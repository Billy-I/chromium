// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/selected_semantic_policy.h"

#include <algorithm>

#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/geometry/transform_state.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/physical_box_fragment.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/scroll/programmatic_scroll_animator.h"
#include "third_party/blink/renderer/core/scroll/scroll_animator_base.h"
#include "third_party/blink/renderer/core/view_transition/view_transition_supplement.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object-inl.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object_cache_impl.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"

namespace blink {
namespace {

using Disposition = SemanticDispositionV1;

bool ChargeSteps(SemanticBudgetV1& budget, unsigned steps) {
  if (budget.relation_steps > 4096 || steps > 4096 - budget.relation_steps) {
    return false;
  }
  budget.relation_steps += steps;
  return true;
}

bool HasExistingScrollAnimation(const ScrollableArea& area) {
  auto* scroll = area.ExistingScrollAnimator();
  auto* programmatic = area.ExistingProgrammaticScrollAnimator();
  return (scroll && scroll->HasRunningAnimation()) ||
         (programmatic && programmatic->HasRunningAnimation());
}

Disposition CheckMappingObject(const LayoutObject& layout,
                               const LayoutView& root,
                               const LayoutText* ordinary_source_text) {
  // Blink leaves the ordinary text self-overflow bit set after line layout.
  // Fragment rectangles are governed by the clean containing box. No other
  // overflow, layout or paint readiness check is waived or prepared here.
  if (layout.NeedsLayout() || layout.ChildNeedsScrollableOverflowRecalc() ||
      (&layout != ordinary_source_text && layout.SelfNeedsScrollableOverflowRecalc()) ||
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
      style.Opacity() != 1 || style.EffectiveZoom() != 1 ||
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

Disposition MapOwnRect(const LayoutBox& source, const LayoutText* contents,
                       const LayoutView& root, unsigned chain_length,
                       PhysicalRect rect, SemanticBudgetV1& budget) {
  // Reserve for each recursive pass, including contents/root adjustments.
  // This bounds relation/clip work, not individual machine instructions.
  if (!ChargeSteps(budget, 3 * (chain_length + 1) + 6)) {
    return Disposition::kLimitExceeded;
  }
  if (contents && &source != &root) {
    TransformState state(TransformState::kApplyTransformDirection,
                         gfx::QuadF(gfx::RectF(rect)));
    if (!source.MapContentsRectToBoxSpace(
            state, TransformState::kFlattenTransform, *contents,
            kDefaultVisualRectFlags)) {
      return Disposition::kOffscreen;
    }
    state.Flatten();
    rect = PhysicalRect::EnclosingRect(state.LastPlanarQuad().BoundingBox());
  }
  // Flags zero disable GeometryMapper's lazy transform cache. The validated
  // static/relative parent chain ends at this exact LayoutView.
  if (!source.MapToVisualRectInAncestorSpace(&root, rect,
                                            kDefaultVisualRectFlags)) {
    return Disposition::kOffscreen;
  }
  if (root.IsScrollContainer()) {
    rect.offset -= root.ScrolledContentOffset();
  }
  rect.Intersect(root.OverflowClipRect(kIgnoreOverlayScrollbarSize));
  return rect.IsEmpty() ? Disposition::kOffscreen : Disposition::kAdmitted;
}

}  // namespace

SemanticDispositionV1 ClassifyOwnGeometryV1(AXObject& object,
                                         SemanticBudgetV1& budget) {
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
  if (document.Printing() || !frame->IsOutermostMainFrame() ||
      frame->LayoutZoomFactor() != 1 || !document.GetPage() ||
      (transitions &&
       transitions->HasPendingOrActiveTransitionsForSemanticCapture()) ||
      (document.GetSettings() && document.GetSettings()
                                     ->GetPlaceRTLScrollbarsOnLeftSideInMainFrame())) {
    return Disposition::kUnsupportedGeometry;
  }
  const auto& viewport = document.GetPage()->GetVisualViewport();
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
  if (!root || budget.nodes >= 256 || budget.depth > 64 ||
      budget.fragments >= 512) {
    return root ? Disposition::kLimitExceeded : Disposition::kNotReady;
  }
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
    if (chain_length >= 64 || budget.nodes >= 256 || !ChargeSteps(budget, 1)) {
      return Disposition::kLimitExceeded;
    }
    ++chain_length;
    ++budget.nodes;
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
    for (;;) {
      if (budget.fragments >= 512 || !ChargeSteps(budget, 1)) {
        return Disposition::kLimitExceeded;
      }
      ++budget.fragments;
      const FragmentItem& item = span[index];
      if (item.GetLayoutObject() != text || item.Type() != FragmentItem::kText ||
          item.IsHiddenForPaint()) {
        return Disposition::kUnsupportedText;
      }
      cursor.MoveTo(item);
      const PhysicalRect rect = cursor.Current().RectInContainerFragment();
      if (!rect.IsEmpty()) {
        has_positive_fragment = true;
        Disposition mapped = MapOwnRect(*text_container, text, *root,
                                       chain_length, rect, budget);
        if (mapped == Disposition::kLimitExceeded) {
          return mapped;
        }
        has_visible_fragment |= mapped == Disposition::kAdmitted;
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
    return Disposition::kOwnZeroSize;
  }
  return MapOwnRect(*box, nullptr, *root, chain_length,
                    PhysicalRect(PhysicalOffset(), size), budget);
}

}  // namespace blink
