// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/selected_semantic_policy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/geometry/transform_state.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/inline/inline_item.h"
#include "third_party/blink/renderer/core/layout/inline/inline_item_span.h"
#include "third_party/blink/renderer/core/layout/inline/inline_node_data.h"
#include "third_party/blink/renderer/core/layout/inline/offset_mapping.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/physical_box_fragment.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/scroll/programmatic_scroll_animator.h"
#include "third_party/blink/renderer/core/scroll/scroll_animator_base.h"
#include "third_party/blink/renderer/core/view_transition/view_transition_supplement.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object-inl.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object_cache_impl.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

#include "third_party/blink/renderer/core/dom/attribute.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/selected_semantic_read_scope.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/modules/accessibility/ax_node_object.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/time/default_tick_clock.h"
#include "base/time/tick_clock.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {
// The only class allowed to arm primitive read permits. Every arming operation
// is immediately followed by its exact getter; no borrowed attribute strings
// or mapping carriers leave this implementation.
class SelectedSemanticPolicyReadAccess {
 public:
  using Disposition = SemanticDispositionV1;
  static Disposition Attributes(const Element& owner,
                                 SelectedSemanticReadScope& scope,
                                 SemanticBudgetV1& budget, bool& has_role) {
    const auto attributes = owner.AttributesWithoutUpdate();
    if (attributes.size() > 256 || budget.relation_steps > 4096 ||
        attributes.size() > 4096 - budget.relation_steps) {
      return Disposition::kLimitExceeded;
    }
    budget.relation_steps += attributes.size();
    for (unsigned index = 0; index < attributes.size(); ++index) {
      const auto& attribute = attributes[index];
      const auto& name = attribute.GetName();
      if (name == html_names::kHiddenAttr || name == html_names::kInertAttr ||
          name == html_names::kDisabledAttr || name == html_names::kAriaModalAttr) {
        return Disposition::kForbiddenAncestor;
      }
      if (name == html_names::kRoleAttr) has_role = true;
      using Purpose = SelectedSemanticReadScope::Purpose;
      Purpose purpose = Purpose::kNone;
      if (name == html_names::kAriaHiddenAttr) purpose = Purpose::kAriaHidden;
      if (name == html_names::kAriaDisabledAttr) purpose = Purpose::kAriaDisabled;
      if (name == html_names::kContenteditableAttr) purpose = Purpose::kContentEditable;
      if (purpose == Purpose::kNone) continue;
      if (!scope.PermitAttribute(owner, owner, index, purpose))
        return Disposition::kNotReady;
      const auto& token = attribute.Value();
      // A reference is consumed only locally. Inspect at most fourteen code
      // units, and never emit structural token content.
      if (token.length() > 14) return Disposition::kLimitExceeded;
      if (EqualIgnoringAsciiCase(token, "false")) continue;
      if (EqualIgnoringAsciiCase(token, "true") ||
          (purpose == Purpose::kContentEditable &&
           (token.empty() || EqualIgnoringAsciiCase(token, "plaintext-only")))) {
        return Disposition::kForbiddenAncestor;
      }
      return Disposition::kUnsupportedText;
    }
    return Disposition::kAdmitted;
  }

  static SemanticNodeResultV1 Content(Node& node,
                                      SelectedSemanticReadScope& scope,
                                      SemanticBudgetV1& budget,
                                      SemanticAuditV1& audit,
                                      bool* malformed_encoding = nullptr) {
    String value;
    if (auto* text = DynamicTo<Text>(node)) {
      if (text->length() > 4096) return {Disposition::kLimitExceeded, {}};
      if (!scope.PermitText(node, *text)) return {};
      value = text->data();
    } else if (auto* element = DynamicTo<Element>(node);
               element && element->HasTagName(html_names::kButtonTag)) {
      const auto attributes = element->AttributesWithoutUpdate();
      if (attributes.size() > 256 || budget.relation_steps > 4096 ||
          attributes.size() > 4096 - budget.relation_steps)
        return {Disposition::kLimitExceeded, {}};
      budget.relation_steps += attributes.size();
      const Attribute* label = nullptr;
      unsigned label_index = 0;
      for (unsigned index = 0; index < attributes.size(); ++index) {
        if (attributes[index].GetName() == html_names::kAriaLabelledbyAttr)
          return {Disposition::kUnsupportedText, {}};
        if (attributes[index].GetName() == html_names::kAriaLabelAttr) {
          label = &attributes[index];
          label_index = index;
        }
      }
      if (!label) return {Disposition::kUnsupportedText, {}};
      if (!scope.PermitAttribute(node, *element, label_index,
                                SelectedSemanticReadScope::Purpose::kButtonLabel))
        return {};
      value = label->Value();
    } else {
      return {Disposition::kUnsupportedText, {}};
    }
    if (!scope.IsClean()) return {};
    if (audit.text_reads != std::numeric_limits<unsigned>::max())
      ++audit.text_reads;
    if (value.empty()) return {Disposition::kUnsupportedText, {}};
    if (value.length() > 4096 || budget.utf8_bytes > 65536)
      return {Disposition::kLimitExceeded, {}};
    // UTF-16 bound limits this conversion's temporary allocation to 16KiB.
    const auto bytes = value.Utf8(Utf8ConversionMode::kStrict);
    if (bytes.empty()) {
      if (malformed_encoding) *malformed_encoding = true;
      return {Disposition::kUnsupportedText, {}};
    }
    if (bytes.size() > 4096 || bytes.size() > 65536 - budget.utf8_bytes)
      return {Disposition::kLimitExceeded, {}};
    budget.utf8_bytes += static_cast<unsigned>(bytes.size());
    return {Disposition::kAdmitted, value};
  }
};

namespace {

using Disposition = SemanticDispositionV1;

// Each exclusion event consumes one already-visited AX branch or candidate.
// A larger total cannot be proven within the document traversal bound.
bool AccountExclusion(SemanticAuditV1& audit, unsigned& counter) {
  const uint64_t total =
      static_cast<uint64_t>(audit.forbidden_structure_prunes) +
      audit.original_zero_size_exclusions +
      audit.original_wholly_offscreen_exclusions;
  if (total >= 256 || counter >= 256)
    return false;
  ++counter;
  return true;
}

bool ChargeSteps(SemanticBudgetV1& budget, unsigned steps) {
  if (budget.relation_steps > 4096 || steps > 4096 - budget.relation_steps) {
    return false;
  }
  budget.relation_steps += steps;
  return true;
}

// Checkpoint-local identities. No key in either table enters the posted result.
class SemanticIdentityLedger {
 public:
  enum class Kind : uint8_t { kNode, kLayout, kAX };

  bool Visit(const void* pointer, Kind kind, SemanticBudgetV1& budget) {
    CHECK(pointer);
    const size_t initial = Hash(pointer, kind) & (slots_.size() - 1);
    for (size_t probe = 0; probe < slots_.size(); ++probe) {
      if (!ChargeSteps(budget, 1)) return false;
      Key& key = slots_[(initial + probe) & (slots_.size() - 1)];
      if (key.pointer == pointer && key.kind == kind) return true;
      if (!key.pointer) {
        if (budget.nodes >= 256) return false;
        key = {pointer, kind};
        ++budget.nodes;
        return true;
      }
    }
    return false;
  }

  bool VisitNode(const Node& node, SemanticBudgetV1& budget) {
    return Visit(&node, Kind::kNode, budget);
  }

  bool VisitLayout(const LayoutObject& layout, SemanticBudgetV1& budget) {
    // Only an exact own layout association shares its DOM source identity.
    const Node* source = layout.GetNode();
    if (source && source->GetLayoutObject() == &layout)
      return VisitNode(*source, budget);
    return Visit(&layout, Kind::kLayout, budget);
  }

  bool VisitAX(const AXObject& object, SemanticBudgetV1& budget) {
    if (const Node* source = object.GetNode())
      return VisitNode(*source, budget);
    return Visit(&object, Kind::kAX, budget);
  }

 private:
  struct Key {
    const void* pointer = nullptr;
    Kind kind = Kind::kNode;
  };
  static size_t Hash(const void* pointer, Kind kind) {
    uint64_t value = reinterpret_cast<uintptr_t>(pointer) >> 3;
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    return static_cast<size_t>(value ^ static_cast<uint64_t>(kind));
  }
  // At most 256 occupied slots; fixed capacity prevents growth or rehash work.
  std::array<Key, 1024> slots_{};
};

class SemanticAXVisitLedger {
 public:
  bool Insert(const AXObject& object, SemanticBudgetV1& budget) {
    uint64_t hash = reinterpret_cast<uintptr_t>(&object) >> 3;
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    const size_t initial = static_cast<size_t>(hash) & (slots_.size() - 1);
    for (size_t probe = 0; probe < slots_.size(); ++probe) {
      if (!ChargeSteps(budget, 1)) return false;
      const AXObject*& slot = slots_[(initial + probe) & (slots_.size() - 1)];
      if (slot == &object) return false;  // Repeated DFS edge or cycle.
      if (!slot) {
        if (count_ >= 256) return false;
        slot = &object;
        ++count_;
        return true;
      }
    }
    return false;
  }

 private:
  std::array<const AXObject*, 1024> slots_{};
  unsigned count_ = 0;
};

bool HasExistingScrollAnimation(const ScrollableArea& area) {
  auto* scroll = area.ExistingScrollAnimator();
  auto* programmatic = area.ExistingProgrammaticScrollAnimator();
  return (scroll && scroll->HasRunningAnimation()) ||
         (programmatic && programmatic->HasRunningAnimation());
}

Disposition CheckMappingObject(const LayoutObject& layout,
                               const LayoutView& root,
                               const LayoutText* ordinary_source_text) {
  // Ordinary Text fragments and static inline wrappers can retain overflow
  // scheduling flags after clean containing-block layout. The own text rect
  // is already in that block's coordinates; neither inline overflow flag is
  // consumed by this mapping path. Never clear those flags or exempt boxes,
  // non-text captures, positioned inlines, layout state or paint state.
  const bool unused_inline_overflow = ordinary_source_text &&
      &layout != ordinary_source_text && layout.IsLayoutInline() &&
      layout.StyleRef().GetPosition() == EPosition::kStatic;
  if (layout.NeedsLayout() ||
      (!unused_inline_overflow && layout.ChildNeedsScrollableOverflowRecalc()) ||
      (&layout != ordinary_source_text && !unused_inline_overflow &&
       layout.SelfNeedsScrollableOverflowRecalc()) ||
      layout.NeedsPaintPropertyUpdate() ||
      layout.DescendantNeedsPaintPropertyUpdate() ||
      layout.SubtreePaintPropertyUpdateReasons()) {
    return Disposition::kNotReady;
  }
  const ComputedStyle& style = layout.StyleRef();
  // Check flatness before helpers whose preserve-3D branch inspects grouping
  // properties. HasMask() itself may lazily compute FillLayer caches.
  if (style.TransformStyle3D() != ETransformStyle3D::kFlat ||
      style.HasTransformRelatedProperty() || style.HasFilter() ||
      style.HasBackdropFilter() || layout.HasReflection() ||
      style.Opacity() != 1 ||
      style.EffectiveZoom() != root.StyleRef().EffectiveZoom() ||
      style.HasCurrentOpacityAnimation() || style.HasCurrentFilterAnimation() ||
      style.HasCurrentBackdropFilterAnimation() ||
      style.HasCurrentClipPathAnimation() ||
      style.HasCurrentBackgroundColorAnimation() ||
      style.HasClipPath() || style.MaskLayers().GetImage() ||
      style.MaskLayers().Next() || style.MaskBoxImage().HasImage() ||
      style.HasIsolation() || style.ContainsPaint() || style.ContainsLayout() ||
      style.ContainsStyle() || !style.IsScrollbarGutterAuto() ||
      style.OverflowClipMarginHasAnEffect() ||
      (&layout != &root && style.GetPosition() != EPosition::kStatic &&
       style.GetPosition() != EPosition::kRelative) ||
      style.GetColumnSpan() == EColumnSpan::kAll || layout.IsInsideMulticol() ||
      layout.IsMulticolContainer() || layout.MightBeInsideFragmentationContext() ||
      layout.IsFragmented() || layout.IsSVG() || layout.IsLayoutReplaced() ||
      layout.IsTable() || layout.IsTablePart() ||
      layout.IsLayoutCustomScrollbarPart()) {
    return Disposition::kUnsupportedGeometry;
  }
  if (const auto* box_model = DynamicTo<LayoutBoxModelObject>(layout)) {
    if (box_model->Layer() && box_model->Layer()->Transform()) {
      return Disposition::kUnsupportedGeometry;
    }
  }
  if (const auto* box = DynamicTo<LayoutBox>(layout)) {
    if (box->PhysicalFragmentCount() != 1 || box->HasControlClip() ||
        box->GetAnchorPositionScrollData() ||
        (box != &root && box->IsEffectiveRootScroller()) ||
        (box->HasNonVisibleOverflow() && style.HasBorderRadius())) {
      return Disposition::kUnsupportedGeometry;
    }
    if (!box->HasCachedSize()) {
      return Disposition::kNotReady;
    }
    auto* area = box->GetScrollableArea();
    if (box->IsScrollContainer() && !area) {
      return Disposition::kNotReady;
    }
    if (area && HasExistingScrollAnimation(*area)) {
      return Disposition::kUnsupportedGeometry;
    }
  } else if (!layout.IsLayoutInline() && !layout.IsText()) {
    return Disposition::kUnsupportedGeometry;
  }
  return Disposition::kAdmitted;
}

static_assert(LayoutUnit::kFractionalBits == 6);
static_assert(std::numeric_limits<float>::radix == 2 &&
              std::numeric_limits<float>::digits >= 24 &&
              std::numeric_limits<float>::is_iec559);

// All float vertices/translations/differences are exact on this 1/64px grid.
// Sum absolute translations, so large offsets cannot hide by cancellation.
Disposition CheckExactMappingDomain(const LayoutBox& source, const LayoutView& root,
                           const PhysicalRect& rect,
                           SemanticBudgetV1& budget) {
  constexpr int64_t kMaxRaw = int64_t{1} << 22;
  auto abs_raw = [](LayoutUnit value) {
    return std::abs(static_cast<int64_t>(value.RawValue()));
  };
  const int64_t x = rect.X().RawValue();
  const int64_t y = rect.Y().RawValue();
  int64_t envelope_x = std::max(std::abs(x),
      std::abs(x + static_cast<int64_t>(rect.Width().RawValue())));
  int64_t envelope_y = std::max(std::abs(y),
      std::abs(y + static_cast<int64_t>(rect.Height().RawValue())));
  unsigned depth = 0;
  for (const LayoutObject* current = &source; current; current = current->Parent()) {
    if (++depth > 64 || !ChargeSteps(budget, 1)) return Disposition::kLimitExceeded;
    if (const auto* box = DynamicTo<LayoutBox>(current)) {
      if (box != &root) {
        const auto location = box->PhysicalLocation();
        envelope_x += abs_raw(location.left);
        envelope_y += abs_raw(location.top);
      }
      // Overcounting the source box's unused scroll in a border-box mapping is
      // conservative. Every actual contents/root translation is included.
      if (box->IsScrollContainer()) {
        const auto* area = box->GetScrollableArea();
        if (!area) return Disposition::kUnsupportedGeometry;
        const auto native = area->GetScrollOffset();
        const auto grid = box->ScrolledContentOffset();
        if (!std::isfinite(native.x()) || !std::isfinite(native.y()) ||
            native.x() != grid.left.ToFloat() ||
            native.y() != grid.top.ToFloat()) return Disposition::kUnsupportedGeometry;
        envelope_x += abs_raw(grid.left);
        envelope_y += abs_raw(grid.top);
      }
    }
    if (envelope_x > kMaxRaw || envelope_y > kMaxRaw) return Disposition::kUnsupportedGeometry;
    if (current == &root) return Disposition::kAdmitted;
  }
  return Disposition::kUnsupportedGeometry;
}

bool MapRectPass(const LayoutBox& source, const LayoutText* contents,
                 const LayoutView& root, PhysicalRect& rect,
                 VisualRectFlags flags, bool clip_viewport) {
  if (contents && &source != &root) {
    TransformState state(TransformState::kApplyTransformDirection,
                         gfx::QuadF(gfx::RectF(rect)));
    if (!source.MapContentsRectToBoxSpace(
            state, TransformState::kFlattenTransform, *contents, flags)) {
      return false;
    }
    state.Flatten();
    rect = PhysicalRect::EnclosingRect(state.LastPlanarQuad().BoundingBox());
  }
  if (!source.MapToVisualRectInAncestorSpace(&root, rect, flags)) return false;
  if (root.IsScrollContainer()) rect.offset -= root.ScrolledContentOffset();
  if (clip_viewport)
    rect.Intersect(root.OverflowClipRect(kIgnoreOverlayScrollbarSize));
  return !rect.IsEmpty();
}

Disposition MapOwnRect(const LayoutBox& source, const LayoutText* contents,
                       const LayoutView& root, unsigned chain_length,
                       PhysicalRect rect, SemanticBudgetV1& budget,
                       bool require_full_coverage,
                       bool* original_wholly_offscreen) {
  if (original_wholly_offscreen)
    *original_wholly_offscreen = false;
  if (!ChargeSteps(budget, (require_full_coverage ? 6 : 3) *
                               (chain_length + 1) +
                               (require_full_coverage ? 12 : 6))) {
    return Disposition::kLimitExceeded;
  }
  if (require_full_coverage) {
    const auto exact = CheckExactMappingDomain(source, root, rect, budget);
    if (exact != Disposition::kAdmitted) return exact;
  }
  const PhysicalRect own_rect = rect;
  PhysicalRect unclipped = rect;
  if (!MapRectPass(source, contents, root, rect, kDefaultVisualRectFlags, true)) {
    // A failed clipped mapping proves no visible intersection only when the
    // same positive original rect maps successfully with clips skipped.
    if (original_wholly_offscreen) {
      if (!require_full_coverage &&
          !ChargeSteps(budget, 3 * (chain_length + 1) + 6)) {
        return Disposition::kLimitExceeded;
      }
      if (MapRectPass(source, contents, root, unclipped,
                      kSkipAncestorAndViewportClips, false)) {
        // The floating-point mapping is a proof only inside the exact 1/64px
        // domain, even for a button whose admission needs no full coverage.
        const auto exact = require_full_coverage
                               ? Disposition::kAdmitted
                               : CheckExactMappingDomain(source, root, own_rect,
                                                         budget);
        if (exact == Disposition::kLimitExceeded)
          return exact;
        *original_wholly_offscreen = exact == Disposition::kAdmitted;
      }
    }
    return Disposition::kOffscreen;
  }
  if (require_full_coverage &&
      (!MapRectPass(source, contents, root, unclipped,
                    kSkipAncestorAndViewportClips, false) || rect != unclipped)) {
    return Disposition::kOffscreen;
  }
  return Disposition::kAdmitted;
}

struct SourceInterval {
  unsigned start = 0;
  unsigned end = 0;
};

Disposition ExistingIdentityInterval(const Text& source, const LayoutText& text,
                                     const LayoutBlockFlow& container,
                                     SemanticBudgetV1& budget,
                                     SourceInterval& interval) {
  if (!source.length() || source.length() > 4096)
    return source.length() ? Disposition::kLimitExceeded
                           : Disposition::kUnsupportedText;
  if (text.IsSecure() || text.HasTextTransform() || text.HasVariableLengthTransform())
    return Disposition::kUnsupportedText;
  const auto* data = container.GetInlineNodeData();
  if (container.NeedsCollectInlines() || !data || !text.HasValidInlineItems())
    return Disposition::kNotReady;
  const auto identity = text.GetSemanticTextIdentity(*data, interval.start, interval.end);
  if (identity == LayoutText::SemanticTextIdentity::kUnavailable)
    return Disposition::kNotReady;
  if (identity != LayoutText::SemanticTextIdentity::kIdentity)
    return Disposition::kUnsupportedText;
  const auto& items = text.InlineItems();
  if (items.empty() || items.size() > 512 ||
      !ChargeSteps(budget, static_cast<unsigned>(items.size())))
    return Disposition::kLimitExceeded;
  unsigned end = interval.start;
  for (const auto& item : items) {
    if (item->GetLayoutObject() != &text || item->Type() != InlineItem::kText ||
        item->TextType() != TextItemType::kNormal ||
        item->IsGeneratedForLineBreak() || item->StartOffset() != end ||
        item->EndOffset() <= end || item->EndOffset() > interval.end)
      return Disposition::kUnsupportedText;
    end = item->EndOffset();
  }
  if (end != interval.end || interval.end < interval.start ||
      interval.end - interval.start != source.length())
    return Disposition::kUnsupportedText;
  return Disposition::kAdmitted;
}

Disposition ClassifyGeometry(AXObject& object, SemanticBudgetV1& budget,
                             bool require_full_text,
                             SemanticIdentityLedger* ledger = nullptr,
                             bool* original_wholly_offscreen = nullptr,
                             bool* original_own_zero_size = nullptr) {
  if (original_wholly_offscreen)
    *original_wholly_offscreen = false;
  if (original_own_zero_size)
    *original_own_zero_size = false;
  if (object.IsDetached()) {
    return Disposition::kStaleDocument;
  }
  Node* node = object.GetNode();
  if (!node || !node->isConnected()) {
    return Disposition::kStaleDocument;
  }
  Document& document = node->GetDocument();
  LocalFrame* frame = document.GetFrame();
  auto& cache = object.AXObjectCache();
  if (!document.IsActive() || !frame || frame->GetDocument() != &document ||
      &cache.GetDocument() != &document) {
    return Disposition::kStaleDocument;
  }
  if (!cache.IsFrozen() || !ScriptForbiddenScope::IsScriptForbidden() ||
      object.NeedsToUpdateCachedValues() ||
      document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean ||
      !cache.Get(&document) || cache.IsDirty()) {
    return Disposition::kNotReady;
  }
  LayoutObject* layout = object.GetLayoutObject();
  if (!layout || layout != node->GetLayoutObject() ||
      layout->GetNode() != node || &layout->GetDocument() != &document) {
    return Disposition::kNotReady;
  }
  const auto* transitions = document.GetViewTransitionsIfExists();
  Page* page = document.GetPage();
  if (document.Printing() || !frame->IsOutermostMainFrame() || !page ||
      (transitions &&
       transitions->HasPendingOrActiveTransitionsForSemanticCapture()) ||
      (document.GetSettings() && document.GetSettings()
                                     ->GetPlaceRTLScrollbarsOnLeftSideInMainFrame())) {
    return Disposition::kUnsupportedGeometry;
  }
  const float device_scale =
      page->GetChromeClient().ZoomFactorForViewportLayout();
  // StyleResolver seeds EffectiveZoom from LayoutZoomFactor. A pure device
  // scale changes layout coordinates but introduces no extra mapping across
  // the flat, same-scale chain. User, widget CSS and descendant CSS zoom stay
  // outside this source-geometry proof.
  if (!std::isfinite(device_scale) || device_scale <= 0 ||
      frame->LayoutZoomFactor() != device_scale ||
      frame->CssZoomFactor() != 1 ||
      page->GetChromeClient().UserZoomFactor(frame) != 1) {
    return Disposition::kUnsupportedGeometry;
  }
  const auto& viewport = page->GetVisualViewport();
  if (viewport.Scale() != 1 || viewport.GetScrollOffset() != ScrollOffset() ||
      viewport.IsPinchGestureActive() || viewport.BrowserControlsAdjustment() ||
      viewport.GetDeviceEmulationTransformNode() ||
      HasExistingScrollAnimation(viewport)) {
    return Disposition::kUnsupportedGeometry;
  }
  if (viewport.NeedsPaintPropertyUpdate()) {
    return Disposition::kNotReady;
  }
  const LayoutView* root = document.GetLayoutView();
  if (!root || (!ledger && budget.nodes >= 256) || budget.depth > 64 ||
      budget.fragments >= 512) {
    return root ? Disposition::kLimitExceeded : Disposition::kNotReady;
  }
  if (root->StyleRef().EffectiveZoom() != device_scale)
    return Disposition::kUnsupportedGeometry;
  const auto* text = DynamicTo<LayoutText>(layout);
  if (text && (!node->IsTextNode() || text->IsTextFragment() || text->IsSVG() ||
               !text->IsInLayoutNGInlineFormattingContext())) {
    return Disposition::kUnsupportedText;
  }
  const LayoutBlockFlow* text_container = nullptr;
  unsigned chain_length = 0;
  for (const LayoutObject* current = layout;; current = current->Parent()) {
    if (!current) {
      return Disposition::kUnsupportedGeometry;
    }
    if (chain_length >= 64 || (!ledger && budget.nodes >= 256) ||
        !ChargeSteps(budget, 1) ||
        (ledger && !ledger->VisitLayout(*current, budget))) {
      return Disposition::kLimitExceeded;
    }
    ++chain_length;
    if (!ledger) ++budget.nodes;
    budget.depth = std::max(budget.depth, chain_length);
    Disposition checked = CheckMappingObject(*current, *root, text);
    if (checked != Disposition::kAdmitted) {
      return checked;
    }
    if (text && current != text && !text_container) {
      if (current->IsLayoutBlock()) {
        text_container = DynamicTo<LayoutBlockFlow>(current);
        if (!text_container) {
          return Disposition::kUnsupportedText;
        }
      } else if (!current->IsLayoutInline() ||
                 current->StyleRef().GetPosition() != EPosition::kStatic) {
        return Disposition::kUnsupportedText;
      }
    }
    if (current == root) {
      break;
    }
  }
  if (text) {
    if (!text_container) {
      return Disposition::kUnsupportedText;
    }
    SourceInterval source_interval;
    Vector<SourceInterval, 8> fragment_intervals;
    if (require_full_text) {
      Disposition mapping = ExistingIdentityInterval(
          To<Text>(*node), *text, *text_container, budget, source_interval);
      if (mapping != Disposition::kAdmitted) return mapping;
    }
    const auto* fragment = text_container->GetPhysicalFragment(0);
    if (!fragment || fragment->GetLayoutObject() != text_container) {
      return Disposition::kNotReady;
    }
    const FragmentItems* items = fragment->Items();
    const auto first = text->FirstInlineFragmentItemIndex();
    if (!items || items->SizeOfEarlierFragments() || !first ||
        first > items->Size()) {
      return Disposition::kUnsupportedText;
    }
    const auto span = items->Items();
    auto index = first - 1;
    InlineCursor cursor(*fragment);
    bool has_positive_fragment = false;
    bool has_visible_fragment = false;
    unsigned mapped_fragments = 0;
    for (;;) {
      if (budget.fragments >= 512 || !ChargeSteps(budget, 1)) {
        return Disposition::kLimitExceeded;
      }
      ++budget.fragments;
      const FragmentItem& item = span[index];
      if (item.GetLayoutObject() != text || item.Type() != FragmentItem::kText ||
          item.IsHiddenForPaint() ||
          (require_full_text &&
           (item.IsGeneratedText() || item.UsesFirstLineStyle()))) {
        return Disposition::kUnsupportedText;
      }
      if (require_full_text) {
        const auto offsets = item.TextOffset();
        if (offsets.start < source_interval.start ||
            offsets.end > source_interval.end || offsets.start >= offsets.end) {
          return Disposition::kUnsupportedText;
        }
        fragment_intervals.push_back(SourceInterval{offsets.start, offsets.end});
      }
      cursor.MoveTo(item);
      const PhysicalRect rect = cursor.Current().RectInContainerFragment();
      if (!rect.IsEmpty()) {
        has_positive_fragment = true;
        ++mapped_fragments;
        bool fragment_wholly_offscreen = false;
        Disposition mapped = MapOwnRect(*text_container, text, *root,
                                       chain_length, rect, budget,
                                       require_full_text,
                                       original_wholly_offscreen
                                           ? &fragment_wholly_offscreen
                                           : nullptr);
        if (mapped == Disposition::kLimitExceeded ||
            (require_full_text && mapped != Disposition::kAdmitted)) {
          // A single positive fragment can prove the whole text candidate is
          // outside all clips. For multiple fragments, the early exclusion
          // does not establish the remaining fragments' original geometry.
          if (original_wholly_offscreen &&
              mapped == Disposition::kOffscreen &&
              fragment_wholly_offscreen &&
              mapped_fragments == 1 &&
              !item.DeltaToNextForSameLayoutObject()) {
            *original_wholly_offscreen = true;
          }
          return mapped;
        }
        has_visible_fragment |= mapped == Disposition::kAdmitted;
      }
      if (require_full_text && rect.IsEmpty()) {
        // A mixed positive/empty text run is excluded, but does not prove
        // that the original candidate's own geometry is wholly zero-sized.
        if (original_own_zero_size && !mapped_fragments &&
            !item.DeltaToNextForSameLayoutObject()) {
          *original_own_zero_size = true;
        }
        return Disposition::kOwnZeroSize;
      }
      const auto delta = item.DeltaToNextForSameLayoutObject();
      if (!delta) {
        break;
      }
      if (delta >= span.size() - index) {
        return Disposition::kUnsupportedText;
      }
      index += delta;
    }
    if (!has_positive_fragment) {
      return Disposition::kOwnZeroSize;
    }
    if (require_full_text) {
      std::sort(fragment_intervals.begin(), fragment_intervals.end(),
                [](const auto& a, const auto& b) { return a.start < b.start; });
      unsigned end = source_interval.start;
      for (const auto& interval : fragment_intervals) {
        if (interval.start != end) return Disposition::kUnsupportedText;
        end = interval.end;
      }
      if (end != source_interval.end) return Disposition::kUnsupportedText;
    }
    return has_visible_fragment ? Disposition::kAdmitted
                                : Disposition::kOffscreen;
  }
  const auto* box = DynamicTo<LayoutBox>(layout);
  if (!box) {
    return Disposition::kUnsupportedGeometry;
  }
  ++budget.fragments;
  const PhysicalBoxFragment* fragment = box->GetPhysicalFragment(0);
  if (!fragment || fragment->GetLayoutObject() != box) {
    return Disposition::kNotReady;
  }
  const PhysicalSize size = fragment->Size();
  if (size.width <= LayoutUnit() || size.height <= LayoutUnit()) {
    if (original_own_zero_size)
      *original_own_zero_size = true;
    return Disposition::kOwnZeroSize;
  }
  return MapOwnRect(*box, nullptr, *root, chain_length,
                    PhysicalRect(PhysicalOffset(), size), budget, false,
                    original_wholly_offscreen);
}

Disposition CaptureState(const Node& source, AXObjectCacheImpl& cache) {
  const auto& document = source.GetDocument();
  const auto* frame = document.GetFrame();
  if (!source.isConnected() || !document.IsActive() || !frame ||
      frame->GetDocument() != &document || &cache.GetDocument() != &document)
    return Disposition::kStaleDocument;
  // IsDirty's internal root access is safe only after the non-creating lookup.
  if (!cache.IsFrozen() || !ScriptForbiddenScope::IsScriptForbidden() ||
      document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean ||
      !cache.Get(&document) || cache.IsDirty()) return Disposition::kNotReady;
  if (!frame->IsOutermostMainFrame()) return Disposition::kForbiddenAncestor;
  if (frame->IsInert() || document.InDesignMode())
    return Disposition::kForbiddenAncestor;
  return Disposition::kAdmitted;
}

bool IsPlainContainer(const Element& element) {
  // Concrete initial taxonomy. Unsupported controls/foreign/generated sources
  // cannot acquire ordinary-text admission through generic ignored AX state.
  using namespace html_names;
  return element.HasTagName(kHTMLTag) || element.HasTagName(kBodyTag) ||
         element.HasTagName(kDivTag) || element.HasTagName(kSpanTag) ||
         element.HasTagName(kPTag) || element.HasTagName(kATag) ||
         element.HasTagName(kMainTag) || element.HasTagName(kSectionTag) ||
         element.HasTagName(kArticleTag) || element.HasTagName(kHeaderTag) ||
         element.HasTagName(kFooterTag) || element.HasTagName(kNavTag) ||
         element.HasTagName(kH1Tag) || element.HasTagName(kH2Tag) ||
         element.HasTagName(kH3Tag) || element.HasTagName(kStrongTag) ||
         element.HasTagName(kEmTag) || element.HasTagName(kBTag) ||
         element.HasTagName(kITag) || element.HasTagName(kLabelTag) ||
         element.HasTagName(kFieldsetTag) || element.HasTagName(kLegendTag) ||
         element.HasTagName(kButtonTag);
}

Disposition ClassifyStructure(Node& source, AXObjectCacheImpl& cache,
                               SemanticBudgetV1& budget,
                               SelectedSemanticReadScope& scope,
                               SemanticIdentityLedger* ledger = nullptr,
                               bool traverse_container = false) {
  Disposition ready = CaptureState(source, cache);
  if (ready != Disposition::kAdmitted) return ready;
  const Document& document = source.GetDocument();
  const auto* modal = cache.GetActiveAriaModalDialog();
  bool within_cached_modal = !modal;
  unsigned depth = 0;
  for (const Node* current = &source; current; current = current->parentNode()) {
    if ((!ledger && budget.nodes >= 256) || ++depth > 64 ||
        !ChargeSteps(budget, 1) ||
        (ledger && !ledger->VisitNode(*current, budget)))
      return Disposition::kLimitExceeded;
    if (!ledger) ++budget.nodes;
    budget.depth = std::max(budget.depth, depth);
    if (&current->GetDocument() != &document || !current->isConnected())
      return Disposition::kStaleDocument;
    if (current->IsPseudoElement() || current->IsInShadowTree() ||
        current->GetCustomElementState() != CustomElementState::kUncustomized)
      return Disposition::kUnsupportedText;
    within_cached_modal |= current == modal;
    if (current == &document)
      return within_cached_modal ? Disposition::kAdmitted
                                 : Disposition::kForbiddenAncestor;
    const AXObject* associated = cache.Get(current);
    if (associated) {
      if (associated->IsDetached() || associated->GetNode() != current)
        return Disposition::kStaleDocument;
      if (associated->NeedsToUpdateCachedValues()) return Disposition::kNotReady;
      if (associated->IsHiddenViaStyle() || associated->IsInert() ||
          associated->IsAriaHidden()) return Disposition::kForbiddenAncestor;
    }
    const auto* element = DynamicTo<Element>(current);
    if (!element) {
      if (current != &source || !current->IsTextNode())
        return Disposition::kUnsupportedText;
      continue;
    }
    if (!element->IsHTMLElement() ||
        element->HasTagName(html_names::kSlotTag))
      return Disposition::kUnsupportedText;
    if (current != &source && element->HasTagName(html_names::kButtonTag))
      return Disposition::kForbiddenAncestor;
    if (!IsPlainContainer(*element)) return Disposition::kForbiddenAncestor;
    if (element->GetShadowRoot()) return Disposition::kUnsupportedText;
    bool has_role = false;
    Disposition attributes = SelectedSemanticPolicyReadAccess::Attributes(
        *element, scope, budget, has_role);
    if (attributes != Disposition::kAdmitted) return attributes;
    if (has_role) {
      const auto* node_object = DynamicTo<AXNodeObject>(associated);
      if (!node_object) return Disposition::kUnsupportedText;
      // Public base dispatch reaches the validated node override, which only
      // returns its cached interpreted role.
      const auto role = associated->RawAriaRole();
      if (role != ax::mojom::blink::Role::kNone &&
          !(role == ax::mojom::blink::Role::kGroup &&
            (current != &source || traverse_container) &&
            !element->HasTagName(html_names::kButtonTag)) &&
          !(element->HasTagName(html_names::kButtonTag) &&
            role == ax::mojom::blink::Role::kButton))
        return Disposition::kUnsupportedText;
    }
    const ComputedStyle* style = element->GetComputedStyle();
    if (!style) return Disposition::kNotReady;
    if (style->Display() == EDisplay::kNone ||
        style->Visibility() != EVisibility::kVisible ||
        style->ContentVisibility() != EContentVisibility::kVisible ||
        style->IsEnsuredInDisplayNone() || style->IsInert() ||
        style->UsedUserModify() != EUserModify::kReadOnly ||
        style->TextSecurity() != ETextSecurity::kNone)
      return Disposition::kForbiddenAncestor;
  }
  return Disposition::kStaleDocument;
}

}  // namespace

SemanticDispositionV1 ClassifyOwnGeometryV1(AXObject& object,
                                            SemanticBudgetV1& budget) {
  return ClassifyGeometry(object, budget, false);
}

namespace {
void AccountScope(const SelectedSemanticReadScope& scope,
                  const SelectedSemanticReadScope::Counts& before,
                  SemanticAuditV1& audit) {
  const auto& after = scope.ReadCounts();
  auto add = [](unsigned& target, unsigned count) {
    target += std::min(count, std::numeric_limits<unsigned>::max() - target);
  };
  add(audit.structural_reads, after.structural - before.structural);
  add(audit.forbidden_reads, after.forbidden - before.forbidden);
}
}  // namespace

SemanticDispositionV1 ClassifySelectedStructureV1(
    Node& source, AXObjectCacheImpl& cache, SemanticBudgetV1& budget,
    SemanticAuditV1& audit) {
  SelectedSemanticReadScope scope(source.GetDocument());
  const auto before = scope.ReadCounts();
  Disposition result = scope.IsClean()
      ? ClassifyStructure(source, cache, budget, scope) : Disposition::kNotReady;
  AccountScope(scope, before, audit);
  return scope.IsClean() ? result : Disposition::kNotReady;
}

namespace {
SemanticNodeResultV1 ReadScopedNode(AXObject& object,
                                    SemanticBudgetV1& budget,
                                    SemanticAuditV1& audit,
                                    SelectedSemanticReadScope& scope,
                                    SemanticIdentityLedger* ledger = nullptr,
                                    bool* malformed_encoding = nullptr,
                                    bool* original_wholly_offscreen = nullptr,
                                    bool* original_own_zero_size = nullptr) {
  if (object.IsDetached() || !object.GetNode())
    return {Disposition::kStaleDocument, {}};
  Node& node = *object.GetNode();
  auto read = [&]() -> SemanticNodeResultV1 {
    if (!scope.IsClean()) return {};
    Disposition ready = CaptureState(node, object.AXObjectCache());
    if (ready != Disposition::kAdmitted) return {ready, {}};
    if (object.NeedsToUpdateCachedValues()) return {};
    // A native tag alone cannot authorize an ARIA-remapped source button.
    const auto role = object.RoleValue();
    if ((node.IsTextNode() && role != ax::mojom::blink::Role::kStaticText) ||
        (!node.IsTextNode() &&
         (!node.HasTagName(html_names::kButtonTag) ||
          role != ax::mojom::blink::Role::kButton)))
      return {Disposition::kUnsupportedText, {}};
    Disposition structure = ClassifyStructure(
        node, object.AXObjectCache(), budget, scope, ledger);
    if (structure != Disposition::kAdmitted) return {structure, {}};
    Disposition geometry = ClassifyGeometry(
        object, budget, node.IsTextNode(), ledger,
        original_wholly_offscreen, original_own_zero_size);
    if (geometry != Disposition::kAdmitted) return {geometry, {}};
    return SelectedSemanticPolicyReadAccess::Content(
        node, scope, budget, audit, malformed_encoding);
  };
  return read();
}
}  // namespace

SemanticNodeResultV1 ReadSelectedSemanticNodeV1(AXObject& object,
                                              SemanticBudgetV1& budget,
                                              SemanticAuditV1& audit) {
  if (object.IsDetached() || !object.GetNode())
    return {Disposition::kStaleDocument, {}};
  SelectedSemanticReadScope scope(object.GetNode()->GetDocument());
  const auto before = scope.ReadCounts();
  SemanticNodeResultV1 result = ReadScopedNode(object, budget, audit, scope);
  AccountScope(scope, before, audit);
  return scope.IsClean() ? result : SemanticNodeResultV1{};
}

namespace {
bool CleanCachedObject(const AXObject& object, AXObjectCacheImpl& cache) {
  return !object.IsDetached() && &object.AXObjectCache() == &cache &&
         !object.NeedsToUpdateCachedValues() &&
         !object.ChildrenNeedToUpdateCachedValues() &&
         !object.HasDirtyDescendants() && !object.NeedsToUpdateChildren();
}

Disposition CheckIncludedParent(const AXObject& child,
                                const AXObject& included_parent,
                                Document& document, AXObjectCacheImpl& cache,
                                SemanticBudgetV1& budget,
                                SemanticIdentityLedger& identities) {
  std::array<const AXObject*, 64> walked{};
  unsigned count = 0;
  for (const AXObject* parent = child.ParentObjectIfPresent(); parent;
       parent = parent->ParentObjectIfPresent()) {
    if (count >= walked.size() || !ChargeSteps(budget, 1))
      return Disposition::kLimitExceeded;
    if (!CleanCachedObject(*parent, cache)) return Disposition::kNotReady;
    for (unsigned i = 0; i < count; ++i) {
      if (!ChargeSteps(budget, 1)) return Disposition::kLimitExceeded;
      if (walked[i] == parent) return Disposition::kStaleDocument;
    }
    walked[count++] = parent;
    budget.depth = std::max(budget.depth, count);
    const Node* source = parent->GetNode();
    if (source &&
        (&source->GetDocument() != &document || cache.Get(source) != parent))
      return Disposition::kStaleDocument;
    if (!identities.VisitAX(*parent, budget))
      return Disposition::kLimitExceeded;
    if (parent->IsIncludedInTree())
      return parent == &included_parent ? Disposition::kAdmitted
                                         : Disposition::kStaleDocument;
  }
  return Disposition::kStaleDocument;
}

SemanticObservationV1 CaptureSelectedSemanticDocumentV1(
    Document& document, AXObjectCacheImpl& cache, SemanticBudgetV1& budget,
    SemanticAuditV1& audit, Vector<SemanticButtonBindingV1>& button_bindings,
    Vector<SemanticTextBindingV1>& text_bindings) {
  SemanticObservationV1 observation;
  button_bindings.clear();
  text_bindings.clear();
  SelectedSemanticReadScope scope(document);
  const auto before = scope.ReadCounts();
  SemanticIdentityLedger identities;
  SemanticAXVisitLedger visited;
  bool malformed_encoding = false;
  auto capture = [&]() -> Disposition {
    if (!scope.IsClean()) return Disposition::kNotReady;
    Disposition ready = CaptureState(document, cache);
    if (ready != Disposition::kAdmitted) return ready;
    const AXObject* root = cache.Get(&document);
    if (!root || !CleanCachedObject(*root, cache) ||
        root->GetNode() != &document ||
        root->RoleValue() != ax::mojom::blink::Role::kRootWebArea ||
        root->ParentObjectIfPresent())
      return Disposition::kNotReady;

    auto descend = [&](auto&& self, const AXObject& object,
                       const AXObject* included_parent,
                       unsigned depth) -> Disposition {
      if (depth > 64 || !ChargeSteps(budget, 1))
        return Disposition::kLimitExceeded;
      budget.depth = std::max(budget.depth, depth);
      if (!CleanCachedObject(object, cache)) return Disposition::kNotReady;
      if (included_parent) {
        if (!object.IsIncludedInTree()) return Disposition::kStaleDocument;
        Disposition parent = CheckIncludedParent(
            object, *included_parent, document, cache, budget, identities);
        if (parent != Disposition::kAdmitted) return parent;
      }
      if (!visited.Insert(object, budget)) return Disposition::kLimitExceeded;
      const Node* source = object.GetNode();
      if (source &&
          (&source->GetDocument() != &document || cache.Get(source) != &object))
        return Disposition::kStaleDocument;
      if (!identities.VisitAX(object, budget))
        return Disposition::kLimitExceeded;
      // No closest-owner rebinding: source-less objects are closed subtrees.
      if (!source) return Disposition::kAdmitted;
      if (source != &document && !source->isConnected())
        return Disposition::kStaleDocument;

      if (source->IsTextNode() || source->HasTagName(html_names::kButtonTag)) {
        if (source->IsTextNode()) {
          const LayoutObject* ax_layout = object.GetLayoutObject();
          const LayoutObject* own_layout = source->GetLayoutObject();
          if (!ax_layout && !own_layout) return Disposition::kAdmitted;
          if (ax_layout != own_layout) return Disposition::kNotReady;
        }
        bool original_wholly_offscreen = false;
        bool original_own_zero_size = false;
        SemanticNodeResultV1 node = ReadScopedNode(
            const_cast<AXObject&>(object), budget, audit, scope, &identities,
            &malformed_encoding, &original_wholly_offscreen,
            &original_own_zero_size);
        if (malformed_encoding || !scope.IsClean())
          return Disposition::kUnsupportedText;
        if (node.disposition == Disposition::kStaleDocument ||
            node.disposition == Disposition::kNotReady ||
            node.disposition == Disposition::kLimitExceeded)
          return node.disposition;
        if (node.disposition == Disposition::kOwnZeroSize &&
            original_own_zero_size &&
            !AccountExclusion(audit,
                              audit.original_zero_size_exclusions)) {
          return Disposition::kLimitExceeded;
        }
        if (node.disposition == Disposition::kOffscreen &&
            original_wholly_offscreen &&
            !AccountExclusion(audit,
                              audit.original_wholly_offscreen_exclusions)) {
          return Disposition::kLimitExceeded;
        }
        if (node.disposition != Disposition::kAdmitted)
          return Disposition::kAdmitted;  // Closed local leaf exclusion.
        const auto bytes = node.text.Utf8(Utf8ConversionMode::kStrict);
        if (bytes.empty()) return Disposition::kUnsupportedText;
        const uint64_t padded = (static_cast<uint64_t>(bytes.size()) + 7) & ~7ULL;
        const uint64_t next = (observation.entries.empty()
            ? 256ULL : observation.reserved_output_bytes) + 64 + padded;
        if (observation.entries.size() >= 256 || next > 65536)
          return Disposition::kLimitExceeded;
        observation.reserved_output_bytes = static_cast<unsigned>(next);
        if (source->HasTagName(html_names::kButtonTag)) {
          button_bindings.push_back(
              SemanticButtonBindingV1{observation.entries.size(),
                                      WeakPersistent<Node>(const_cast<Node*>(source))});
        } else {
          text_bindings.push_back(
              SemanticTextBindingV1{observation.entries.size(),
                                    WeakPersistent<Node>(
                                        const_cast<Node*>(source))});
        }
        observation.entries.push_back(SemanticObservationEntryV1{source->IsTextNode()
            ? SemanticObservationRoleV1::kText
            : SemanticObservationRoleV1::kButton, std::move(node.text)});
        return Disposition::kAdmitted;
      }

      if (source != &document) {
        if (!DynamicTo<Element>(source)) return Disposition::kAdmitted;
        Disposition structural = ClassifyStructure(
            *const_cast<Node*>(source), cache, budget, scope, &identities,
            /*traverse_container=*/true);
        if (structural == Disposition::kForbiddenAncestor &&
            !AccountExclusion(audit,
                              audit.forbidden_structure_prunes)) {
          return Disposition::kLimitExceeded;
        }
        if (structural == Disposition::kForbiddenAncestor ||
            structural == Disposition::kUnsupportedText)
          return Disposition::kAdmitted;  // Entire forbidden subtree.
        if (structural != Disposition::kAdmitted) return structural;
      }
      const auto& children = object.ChildrenIncludingIgnored();
      if (children.size() > 256) return Disposition::kLimitExceeded;
      for (const auto& member : children) {
        const AXObject* child = member.Get();
        if (!child) return Disposition::kStaleDocument;
        Disposition branch = self(self, *child, &object, depth + 1);
        if (branch != Disposition::kAdmitted) return branch;
      }
      return Disposition::kAdmitted;
    };
    return descend(descend, *root, nullptr, 1);
  };
  Disposition result = capture();
  AccountScope(scope, before, audit);
  if (!scope.IsClean()) result = Disposition::kNotReady;
  if (malformed_encoding) result = Disposition::kUnsupportedText;
  if (result != Disposition::kAdmitted) {
    observation = {};
    button_bindings.clear();
    text_bindings.clear();
    observation.disposition = result;
  } else {
    observation.disposition = Disposition::kAdmitted;
    if (observation.entries.empty()) observation.reserved_output_bytes = 256;
  }
  return observation;
}
}  // namespace

std::unique_ptr<SelectedSemanticRequestV1> SelectedSemanticRequestV1::Create(
    Document& document, AXObjectCacheImpl& cache, Node& node, uint64_t epoch,
    base::TimeTicks deadline,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner, Completion completion) {
  CHECK(IsMainThread());
  auto request = std::unique_ptr<SelectedSemanticRequestV1>(
      new SelectedSemanticRequestV1(document, cache, node, epoch, deadline,
                                    std::move(task_runner), std::move(completion),
                                    base::DefaultTickClock::GetInstance()));
  request->Start();
  return request;
}

std::unique_ptr<SelectedSemanticRequestV1>
SelectedSemanticRequestV1::CreateDocument(
    Document& document, AXObjectCacheImpl& cache, uint64_t epoch,
    base::TimeTicks deadline,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    Completion completion) {
  CHECK(IsMainThread());
  auto request = std::unique_ptr<SelectedSemanticRequestV1>(
      new SelectedSemanticRequestV1(
          document, cache, document, epoch, deadline, std::move(task_runner),
          std::move(completion), base::DefaultTickClock::GetInstance(),
          SemanticRequestKindV1::kDocument));
  request->Start();
  return request;
}

SelectedSemanticRequestV1::SelectedSemanticRequestV1(
    Document& document, AXObjectCacheImpl& cache, Node& node, uint64_t epoch,
    base::TimeTicks deadline,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner, Completion completion,
    const base::TickClock* clock, SemanticRequestKindV1 kind)
    : document_(&document), frame_(document.GetFrame()), node_(&node),
      cache_(&cache), clock_(clock), epoch_(epoch), kind_(kind), deadline_(deadline),
      task_runner_(std::move(task_runner)), completion_(std::move(completion)),
      timer_(clock_) {
  CHECK(task_runner_ && task_runner_->RunsTasksInCurrentSequence());
  CHECK(completion_ && clock_);
  timer_.SetTaskRunner(task_runner_);
}

SelectedSemanticRequestV1::~SelectedSemanticRequestV1() {
  CHECK(IsMainThread() && task_runner_->RunsTasksInCurrentSequence());
  // Commit a pointer-free cancellation before invalidating member storage.
  if (!terminal_) Cancel();
}

void SelectedSemanticRequestV1::Start() {
  const auto now = clock_->NowTicks();
  SemanticRequestResultV1 result;
  if (!epoch_ || deadline_.is_null() || deadline_ > now + base::Seconds(5)) {
    Finish(std::move(result));
    return;
  }
  if (now >= deadline_) {
    result.terminal = SemanticRequestTerminalV1::kDeadlineExceeded;
    Finish(std::move(result));
    return;
  }
  if (!document_ || !frame_ || !node_ || !cache_ ||
      !document_->IsActive() || !frame_->IsAttached() ||
      frame_->GetDocument() != document_.Get() ||
      document_->GetFrame() != frame_.Get() ||
      &node_->GetDocument() != document_.Get() || !node_->isConnected() ||
      &cache_->GetDocument() != document_.Get() ||
      document_->ExistingAXObjectCache() != cache_.Get()) {
    result.terminal = SemanticRequestTerminalV1::kStaleContext;
    Finish(std::move(result));
    return;
  }
  // Token initialization is admission work outside the measured ready callback.
  document_token_ = document_->Token();
  frame_token_ = frame_->GetLocalFrameToken();
  const auto arm_now = clock_->NowTicks();
  if (arm_now >= deadline_) {
    result.terminal = SemanticRequestTerminalV1::kDeadlineExceeded;
    Finish(std::move(result));
    return;
  }
  timer_.Start(FROM_HERE, deadline_ - arm_now,
               base::BindOnce(&SelectedSemanticRequestV1::OnDeadline,
                              weak_factory_.GetWeakPtr()));
  cache_->ScheduleAXUpdateWithCallback(
      base::BindOnce(&SelectedSemanticRequestV1::OnAXReady,
                     weak_factory_.GetWeakPtr()));
}

bool SelectedSemanticRequestV1::HasCurrentOwnership() const {
  return document_ && frame_ && node_ && cache_ && document_token_ && frame_token_ &&
         document_->IsActive() && frame_->IsAttached() &&
         document_->GetFrame() == frame_.Get() &&
         frame_->GetDocument() == document_.Get() &&
         document_->Token() == *document_token_ &&
         frame_->GetLocalFrameToken() == *frame_token_ &&
         &node_->GetDocument() == document_.Get() && node_->isConnected() &&
         &cache_->GetDocument() == document_.Get() &&
         document_->ExistingAXObjectCache() == cache_.Get();
}

void SelectedSemanticRequestV1::OnAXReady() {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  if (terminal_) return;
  SemanticRequestResultV1 result;
  if (clock_->NowTicks() >= deadline_) {
    result.terminal = SemanticRequestTerminalV1::kDeadlineExceeded;
    Finish(std::move(result));
    return;
  }
  if (!HasCurrentOwnership()) {
    result.terminal = SemanticRequestTerminalV1::kStaleContext;
    Finish(std::move(result));
    return;
  }
  result.terminal = SemanticRequestTerminalV1::kPolicyResult;
  // A manually invoked/non-frozen callback is not a policy capture. Return the
  // default NotReady node without asking AX to create or update anything.
  if (cache_->IsFrozen()) {
    ScriptForbiddenScope forbid_script;
    if (kind_ == SemanticRequestKindV1::kDocument) {
      const uint64_t before_tree_version = document_->DomTreeVersion();
      const uint64_t before_style_version = document_->StyleVersion();
      const uint64_t before_layout_generation =
          document_->View()
              ? document_->View()->LayoutGenerationForSelectedSemantic()
              : 0;
      result.observation = CaptureSelectedSemanticDocumentV1(
          *document_, *cache_, result.budget, result.audit, button_bindings_,
          text_bindings_);
      if (result.observation.disposition == SemanticDispositionV1::kAdmitted &&
          document_->DomTreeVersion() == before_tree_version &&
          document_->StyleVersion() == before_style_version &&
          before_layout_generation && document_->View() &&
          document_->View()->LayoutGenerationForSelectedSemantic() ==
              before_layout_generation) {
        const auto snapshot = CaptureScrollSnapshot(result.budget);
        if (snapshot == SemanticDispositionV1::kAdmitted &&
            document_->View()->LayoutGenerationForSelectedSemantic() ==
                before_layout_generation) {
          result.document_tree_version = before_tree_version;
          result.document_style_version = before_style_version;
          result.document_layout_generation = before_layout_generation;
          layout_generation_snapshot_ = before_layout_generation;
        } else {
          result.observation = {};
          result.observation.disposition =
              snapshot == SemanticDispositionV1::kAdmitted
                  ? SemanticDispositionV1::kStaleDocument
                  : snapshot;
          button_bindings_.clear();
          text_bindings_.clear();
        }
      } else if (result.observation.disposition ==
                 SemanticDispositionV1::kAdmitted) {
        result.terminal = SemanticRequestTerminalV1::kStaleContext;
      }
    } else if (AXObject* object = cache_->Get(node_.Get()))
      result.node = ReadSelectedSemanticNodeV1(*object, result.budget, result.audit);
    else
      result.node.disposition = SemanticDispositionV1::kStaleDocument;
  }
  // Preserve honest read counts if a valid synchronous capture crosses expiry.
  if (clock_->NowTicks() >= deadline_)
    result.terminal = SemanticRequestTerminalV1::kDeadlineExceeded;
  Finish(std::move(result));
}

void SelectedSemanticRequestV1::OnDeadline() {
  if (terminal_) return;
  SemanticRequestResultV1 result;
  result.terminal = SemanticRequestTerminalV1::kDeadlineExceeded;
  Finish(std::move(result));
}

void SelectedSemanticRequestV1::Cancel() {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  if (terminal_) return;
  SemanticRequestResultV1 result;
  result.terminal = SemanticRequestTerminalV1::kCancelled;
  Finish(std::move(result));
}

void SelectedSemanticRequestV1::InvalidateEpoch(uint64_t current_epoch) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  if (terminal_ || current_epoch == epoch_) return;
  SemanticRequestResultV1 result;
  result.terminal = SemanticRequestTerminalV1::kStaleContext;
  Finish(std::move(result));
}

void SelectedSemanticRequestV1::Finish(SemanticRequestResultV1 result) {
  if (terminal_) return;
  terminal_ = true;
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  result.epoch = epoch_;
  result.kind = kind_;
  if (result.terminal != SemanticRequestTerminalV1::kPolicyResult) {
    result.node = {};
    result.observation = {};
    button_bindings_.clear();
    text_bindings_.clear();
    result.document_tree_version = 0;
    result.document_style_version = 0;
    result.document_layout_generation = 0;
    layout_generation_snapshot_ = 0;
    scroll_snapshot_valid_ = false;
    scroll_bindings_.clear();
  } else if (kind_ == SemanticRequestKindV1::kDocument) {
    result.node = {};
    if (result.observation.disposition != SemanticDispositionV1::kAdmitted) {
      button_bindings_.clear();
      text_bindings_.clear();
      result.document_tree_version = 0;
      result.document_style_version = 0;
      result.document_layout_generation = 0;
      layout_generation_snapshot_ = 0;
      scroll_snapshot_valid_ = false;
      scroll_bindings_.clear();
    }
  }
  // Terminal commit is this request's cancellation linearization point. A
  // higher-level session can still revoke the queued result at delivery.
  // The result and request-added
  // delivery arguments contain no DOM/request pointers. Callback capture
  // lifetime remains the caller's responsibility; the client is never called inline.
  // Delivery is conditional on the documented accepting-runner precondition.
  // A runner shutdown never turns into inline client code inside frozen AX.
  task_runner_->PostTask(FROM_HERE,
      base::BindOnce(std::move(completion_), std::move(result)));
}

Vector<SemanticButtonBindingV1>
SelectedSemanticRequestV1::TakeButtonBindings() {
  CHECK(IsMainThread() && task_runner_->RunsTasksInCurrentSequence());
  CHECK(terminal_ && kind_ == SemanticRequestKindV1::kDocument);
  return std::move(button_bindings_);
}

Vector<SemanticTextBindingV1> SelectedSemanticRequestV1::TakeTextBindings() {
  CHECK(IsMainThread() && task_runner_->RunsTasksInCurrentSequence());
  CHECK(terminal_ && kind_ == SemanticRequestKindV1::kDocument);
  return std::move(text_bindings_);
}

SemanticDispositionV1 SelectedSemanticRequestV1::CaptureScrollSnapshot(
    SemanticBudgetV1& budget) {
  CHECK(IsMainThread() && task_runner_->RunsTasksInCurrentSequence());
  scroll_snapshot_valid_ = false;
  scroll_bindings_.clear();
  LocalFrameView* view = document_->View();
  if (!view || view != frame_->View() || !document_->GetPage() ||
      !view->GetScrollableArea()) {
    return SemanticDispositionV1::kNotReady;
  }
  const auto& areas = view->ScrollableAreas();
  if (areas.size() > 256)
    return SemanticDispositionV1::kLimitExceeded;
  // Reserve both capture and later delivery comparisons under the same
  // relation-step ceiling before inspecting any area.
  if (!ChargeSteps(budget, static_cast<unsigned>(2 * areas.size() + 2)))
    return SemanticDispositionV1::kLimitExceeded;
  auto& visual = document_->GetPage()->GetVisualViewport();
  scroll_view_ = view;
  layout_scroll_offset_ = view->GetScrollableArea()->GetScrollOffset();
  layout_visible_rect_ =
      view->GetScrollableArea()->VisibleContentRect(kExcludeScrollbars);
  visual_scroll_offset_ = visual.GetScrollOffset();
  visual_visible_rect_ = visual.VisibleContentRect(kExcludeScrollbars);
  visual_scale_ = visual.Scale();
  for (const auto& area : areas.Values()) {
    if (!area || HasExistingScrollAnimation(*area)) {
      scroll_bindings_.clear();
      return SemanticDispositionV1::kUnsupportedGeometry;
    }
    scroll_bindings_.push_back(SemanticScrollBindingV1{
        WeakPersistent<PaintLayerScrollableArea>(area.Get()),
        area->GetScrollOffset()});
  }
  scroll_snapshot_valid_ = true;
  return SemanticDispositionV1::kAdmitted;
}

bool SelectedSemanticRequestV1::HasCurrentScrollGeometry() const {
  CHECK(IsMainThread() && task_runner_->RunsTasksInCurrentSequence());
  if (!scroll_snapshot_valid_ || !HasCurrentOwnership() ||
      !scroll_view_ || document_->View() != scroll_view_.Get() ||
      !layout_generation_snapshot_ ||
      scroll_view_->LayoutGenerationForSelectedSemantic() !=
          layout_generation_snapshot_ ||
      !document_->GetPage() || document_->NeedsLayoutTreeUpdate() ||
      scroll_view_->NeedsLayout() || !scroll_view_->GetScrollableArea()) {
    return false;
  }
  const auto& visual = document_->GetPage()->GetVisualViewport();
  if (visual.NeedsPaintPropertyUpdate() || visual.IsPinchGestureActive() ||
      visual.BrowserControlsAdjustment() ||
      visual.GetDeviceEmulationTransformNode() ||
      HasExistingScrollAnimation(visual) ||
      visual.Scale() != visual_scale_ ||
      visual.GetScrollOffset() != visual_scroll_offset_ ||
      visual.VisibleContentRect(kExcludeScrollbars) != visual_visible_rect_ ||
      scroll_view_->GetScrollableArea()->GetScrollOffset() !=
          layout_scroll_offset_ ||
      scroll_view_->GetScrollableArea()->VisibleContentRect(
          kExcludeScrollbars) != layout_visible_rect_) {
    return false;
  }
  const auto& areas = scroll_view_->ScrollableAreas();
  if (areas.size() != scroll_bindings_.size())
    return false;
  unsigned index = 0;
  for (const auto& area : areas.Values()) {
    const auto& captured = scroll_bindings_[index++];
    if (!area || area.Get() != captured.area.Get() ||
        area->GetScrollOffset() != captured.offset ||
        HasExistingScrollAnimation(*area)) {
      return false;
    }
  }
  return true;
}

}  // namespace blink
