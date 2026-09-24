// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/selected_semantic_policy.h"
#include "third_party/blink/public/web/web_selected_semantic_session.h"

#include "third_party/blink/renderer/core/layout/layout_object_inlines.h"
#include "third_party/blink/renderer/core/layout/layout_invalidation_reason.h"
#include "third_party/blink/renderer/platform/graphics/subtree_paint_property_update_reason.h"

#include <memory>
#include <limits>
#include <optional>
#include <set>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/core/accessibility/ax_context.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/html/html_dialog_element.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/layout/inline/inline_item.h"
#include "third_party/blink/renderer/core/layout/inline/inline_item_span.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/dom/selected_semantic_read_scope.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/frame_test_helpers.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/html_style_element.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/inline/inline_node_data.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_image.h"
#include "third_party/blink/renderer/core/layout/layout_image_resource.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/physical_box_fragment.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/loader/resource/image_resource_content.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/testing/scoped_mock_overlay_scrollbars.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/core/view_transition/dom_view_transition.h"
#include "third_party/blink/renderer/core/view_transition/scoped_view_transition.h"
#include "third_party/blink/renderer/core/view_transition/view_transition_supplement.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object.h"
#include "third_party/blink/renderer/modules/accessibility/ax_enums.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object-inl.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object_cache_impl.h"
#include "third_party/blink/renderer/modules/accessibility/testing/accessibility_test.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/heap/thread_state.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/graphics/unaccelerated_static_bitmap_image.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

#include <array>
#include <vector>
#include "base/check.h"
#include "base/functional/bind.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/gfx/geometry/size.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_entry.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/layout/inline/fragment_item.h"
#include "third_party/blink/renderer/core/layout/inline/fragment_items.h"
#include "third_party/blink/renderer/core/testing/sim/sim_request.h"
#include "third_party/blink/renderer/core/testing/sim/sim_page.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include <atomic>
#include <vector>
#include "base/run_loop.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/tick_clock.h"
#include "ui/accessibility/ax_action_data.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace blink {

namespace {

enum class ExpectedTransitionState { kUnchecked, kDocument, kWithoutDocument };
enum class ReadinessProbe {
  kNone, kRetainedTextSelfOverflow, kTextChildOverflow,
  kAncestorSelfOverflow, kAncestorChildOverflow, kTextPaint
};

void ExpectFrozenGeometry(AXObjectCacheImpl& cache, Node* target,
                          SemanticDispositionV1 expected,
                          bool invalidate_size = false,
                          ExpectedTransitionState transition_state =
                              ExpectedTransitionState::kUnchecked,
                          ReadinessProbe probe = ReadinessProbe::kNone) {
  using Result = std::optional<SemanticDispositionV1>;
  auto result = std::make_shared<Result>();
  cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> weak_cache,
         WeakPersistent<Node> target, std::shared_ptr<Result> result,
         bool invalidate_size, ExpectedTransitionState transition_state,
         ReadinessProbe probe) {
        ASSERT_NE(nullptr, weak_cache.Get());
        ASSERT_NE(nullptr, target.Get());
        ASSERT_TRUE(weak_cache->IsFrozen());
        ScriptForbiddenScope forbid_script;
        AXObject* object = weak_cache->Get(target.Get());
        ASSERT_NE(nullptr, object);
        LayoutBox* invalidated_box = nullptr;
        if (invalidate_size) {
          invalidated_box = DynamicTo<LayoutBox>(object->GetLayoutObject());
          ASSERT_NE(nullptr, invalidated_box);
          const auto count = invalidated_box->PhysicalFragmentCount();
          ASSERT_EQ(1u, count);
          invalidated_box->ShrinkLayoutResults(count);
          ASSERT_FALSE(invalidated_box->HasCachedSize());
          ASSERT_EQ(count, invalidated_box->PhysicalFragmentCount());
        }
        if (transition_state != ExpectedTransitionState::kUnchecked) {
          auto* transitions = target->GetDocument().GetViewTransitionsIfExists();
          ASSERT_NE(nullptr, transitions);
          ASSERT_TRUE(transitions->HasPendingOrActiveTransitionsForSemanticCapture());
          EXPECT_EQ(transition_state == ExpectedTransitionState::kDocument,
                    transitions->GetTransition() != nullptr);
        }
        LayoutObject* probe_layout = object->GetLayoutObject();
        if (probe != ReadinessProbe::kNone) {
          ASSERT_TRUE(probe_layout && probe_layout->IsText());
          ASSERT_TRUE(probe_layout->SelfNeedsScrollableOverflowRecalc());
          ASSERT_FALSE(probe_layout->ChildNeedsScrollableOverflowRecalc());
          ASSERT_FALSE(probe_layout->Parent()->NeedsScrollableOverflowRecalc());
          switch (probe) {
            case ReadinessProbe::kTextChildOverflow:
              probe_layout->SetChildNeedsScrollableOverflowRecalc();
              break;
            case ReadinessProbe::kAncestorSelfOverflow:
              probe_layout->Parent()->SetSelfNeedsScrollableOverflowRecalc();
              break;
            case ReadinessProbe::kAncestorChildOverflow:
              probe_layout->Parent()->SetChildNeedsScrollableOverflowRecalc();
              break;
            case ReadinessProbe::kTextPaint:
              probe_layout->SetNeedsPaintPropertyUpdate();
              break;
            default:
              break;
          }
        }
        SemanticBudgetV1 budget;
        *result = ClassifyOwnGeometryV1(*object, budget);
        if (probe != ReadinessProbe::kNone) {
          EXPECT_TRUE(probe_layout->SelfNeedsScrollableOverflowRecalc());
          if (probe == ReadinessProbe::kTextChildOverflow)
            EXPECT_TRUE(probe_layout->ChildNeedsScrollableOverflowRecalc());
          if (probe == ReadinessProbe::kAncestorSelfOverflow)
            EXPECT_TRUE(probe_layout->Parent()->SelfNeedsScrollableOverflowRecalc());
          if (probe == ReadinessProbe::kAncestorChildOverflow)
            EXPECT_TRUE(probe_layout->Parent()->ChildNeedsScrollableOverflowRecalc());
          if (probe == ReadinessProbe::kTextPaint)
            EXPECT_TRUE(probe_layout->NeedsPaintPropertyUpdate());
        }
        if (invalidated_box) {
          EXPECT_FALSE(invalidated_box->HasCachedSize());
        }
      },
      WrapWeakPersistent(&cache),
      WrapWeakPersistent(target), result, invalidate_size, transition_state, probe));
  // Ambient test driver only. Production capture cannot force an AX cycle.
  cache.MarkDocumentDirty();
  cache.UpdateAXForAllDocuments();
  ASSERT_TRUE(result->has_value());
  EXPECT_EQ(expected, result->value());
}

}  // namespace

// Explicit test-only cache-state fault injection on the actual AX object.
// No derived object is created or used to downcast a production instance.
struct SemanticAXCacheStateTestAccess : AXObject {
  static void SetDirty(AXObject& object, bool dirty) {
    auto setter = &SemanticAXCacheStateTestAccess::SetCachedValuesNeedUpdate;
    (object.*setter)(dirty, dirty
        ? std::optional<TreeUpdateReason>(TreeUpdateReason::kMarkAXObjectDirty)
        : std::nullopt);
  }
  static Member<AXObject>& Parent(AXObject& object) {
    auto field = &SemanticAXCacheStateTestAccess::parent_;
    return object.*field;
  }
  static AXObjectVector& Children(AXObject& object) {
    auto field = &SemanticAXCacheStateTestAccess::children_;
    return object.*field;
  }
};

enum class SemanticFault {
  kInvalidateItems, kClearItems, kWrongData, kCompareDuringObserver,
  kDirtySourceAX, kDirtyAncestorAX
};

class SelectedSemanticPolicyTest : public AccessibilityTest {
 protected:
  enum class InlineReadinessFault {
    kNone,
    kSourceTextChildOverflow,
    kContainingBoxChildOverflow,
    kContainingBoxSelfOverflow,
    kLayout,
    kOwnPaint,
    kDescendantPaint,
    kSubtreePaint,
  };

  void ExpectStaticInlineReadiness(InlineReadinessFault fault) {
    auto& cache = GetAXObjectCache();
    auto ran = std::make_shared<bool>(false);
    cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> weak_cache,
           WeakPersistent<Node> target, InlineReadinessFault fault,
           std::shared_ptr<bool> ran) {
          ASSERT_NE(nullptr, weak_cache.Get());
          ASSERT_NE(nullptr, target.Get());
          ASSERT_TRUE(weak_cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          AXObject* object = weak_cache->Get(target.Get());
          ASSERT_NE(nullptr, object);
          ASSERT_FALSE(object->NeedsToUpdateCachedValues());
          auto* text = DynamicTo<LayoutText>(target->GetLayoutObject());
          ASSERT_NE(nullptr, text);
          ASSERT_EQ(text, object->GetLayoutObject());
          ASSERT_FALSE(text->IsTextFragment());
          ASSERT_FALSE(text->IsSVG());
          ASSERT_TRUE(text->IsInLayoutNGInlineFormattingContext());
          LayoutObject* wrapper = text->Parent();
          ASSERT_NE(nullptr, wrapper);
          ASSERT_TRUE(wrapper->IsLayoutInline());
          ASSERT_EQ(EPosition::kStatic, wrapper->StyleRef().GetPosition());
          // This is observed ambient state, never manufactured by the test.
          ASSERT_TRUE(wrapper->SelfNeedsScrollableOverflowRecalc());
          ASSERT_TRUE(wrapper->ChildNeedsScrollableOverflowRecalc());
          ASSERT_FALSE(text->ChildNeedsScrollableOverflowRecalc());
          ASSERT_FALSE(wrapper->NeedsLayout());
          ASSERT_FALSE(wrapper->NeedsPaintPropertyUpdate());
          ASSERT_FALSE(wrapper->DescendantNeedsPaintPropertyUpdate());
          ASSERT_EQ(0u, wrapper->SubtreePaintPropertyUpdateReasons());
          // The fixture has exactly one static span within its containing div.
          auto* container = DynamicTo<LayoutBlockFlow>(wrapper->Parent());
          ASSERT_NE(nullptr, container);
          ASSERT_EQ(1u, container->PhysicalFragmentCount());
          ASSERT_TRUE(container->HasCachedSize());
          ASSERT_FALSE(container->NeedsCollectInlines());
          ASSERT_NE(nullptr, container->GetInlineNodeData());
          for (const LayoutObject* current = container; current;
               current = current->Parent()) {
            ASSERT_FALSE(current->NeedsLayout());
            ASSERT_FALSE(current->SelfNeedsScrollableOverflowRecalc());
            ASSERT_FALSE(current->ChildNeedsScrollableOverflowRecalc());
            ASSERT_FALSE(current->NeedsPaintPropertyUpdate());
            ASSERT_FALSE(current->DescendantNeedsPaintPropertyUpdate());
            ASSERT_EQ(0u, current->SubtreePaintPropertyUpdateReasons());
          }
          switch (fault) {
            case InlineReadinessFault::kNone:
              break;
            case InlineReadinessFault::kSourceTextChildOverflow:
              text->SetChildNeedsScrollableOverflowRecalc();
              ASSERT_TRUE(text->ChildNeedsScrollableOverflowRecalc());
              break;
            case InlineReadinessFault::kContainingBoxChildOverflow:
              container->SetChildNeedsScrollableOverflowRecalc();
              ASSERT_TRUE(container->ChildNeedsScrollableOverflowRecalc());
              break;
            case InlineReadinessFault::kContainingBoxSelfOverflow:
              container->SetSelfNeedsScrollableOverflowRecalc();
              ASSERT_TRUE(container->SelfNeedsScrollableOverflowRecalc());
              break;
            case InlineReadinessFault::kLayout:
              wrapper->SetNeedsLayout(layout_invalidation_reason::kUnknown,
                                      kMarkOnlyThis);
              ASSERT_TRUE(wrapper->NeedsLayout());
              break;
            case InlineReadinessFault::kOwnPaint:
              wrapper->SetNeedsPaintPropertyUpdate();
              ASSERT_TRUE(wrapper->NeedsPaintPropertyUpdate());
              break;
            case InlineReadinessFault::kDescendantPaint:
              wrapper->SetDescendantNeedsPaintPropertyUpdate();
              ASSERT_TRUE(wrapper->DescendantNeedsPaintPropertyUpdate());
              break;
            case InlineReadinessFault::kSubtreePaint:
              wrapper->AddSubtreePaintPropertyUpdateReason(
                  SubtreePaintPropertyUpdateReason::kPreviouslySkipped);
              ASSERT_NE(0u, wrapper->SubtreePaintPropertyUpdateReasons());
              break;
          }
          // Capture snapshots AFTER injection: public setters can propagate.
          const bool before_text_child = text->ChildNeedsScrollableOverflowRecalc();
          const bool before_box_self = container->SelfNeedsScrollableOverflowRecalc();
          const bool before_box_child = container->ChildNeedsScrollableOverflowRecalc();
          const bool before_layout = wrapper->NeedsLayout();
          const bool before_child = wrapper->ChildNeedsScrollableOverflowRecalc();
          const bool before_own_paint = wrapper->NeedsPaintPropertyUpdate();
          const bool before_descendant = wrapper->DescendantNeedsPaintPropertyUpdate();
          const unsigned before_subtree = wrapper->SubtreePaintPropertyUpdateReasons();
          SemanticBudgetV1 budget;
          SemanticAuditV1 audit;
          const auto result = ReadSelectedSemanticNodeV1(*object, budget, audit);
          if (fault == InlineReadinessFault::kNone) {
            EXPECT_EQ(SemanticDispositionV1::kAdmitted, result.disposition);
            EXPECT_EQ("Allowed text", result.text);
            EXPECT_EQ(1u, audit.text_reads);
          } else {
            EXPECT_EQ(SemanticDispositionV1::kNotReady, result.disposition);
            EXPECT_TRUE(result.text.empty());
            EXPECT_EQ(0u, audit.text_reads);
            EXPECT_EQ(0u, budget.utf8_bytes);
          }
          EXPECT_EQ(0u, audit.forbidden_reads);
          EXPECT_TRUE(wrapper->SelfNeedsScrollableOverflowRecalc());
          EXPECT_TRUE(wrapper->ChildNeedsScrollableOverflowRecalc());
          EXPECT_EQ(before_text_child, text->ChildNeedsScrollableOverflowRecalc());
          EXPECT_EQ(before_box_self, container->SelfNeedsScrollableOverflowRecalc());
          EXPECT_EQ(before_box_child, container->ChildNeedsScrollableOverflowRecalc());
          EXPECT_EQ(before_layout, wrapper->NeedsLayout());
          EXPECT_EQ(before_child, wrapper->ChildNeedsScrollableOverflowRecalc());
          EXPECT_EQ(before_own_paint, wrapper->NeedsPaintPropertyUpdate());
          EXPECT_EQ(before_descendant, wrapper->DescendantNeedsPaintPropertyUpdate());
          EXPECT_EQ(before_subtree, wrapper->SubtreePaintPropertyUpdateReasons());
          *ran = true;
        }, WrapWeakPersistent(&cache), WrapWeakPersistent(TargetText()), fault,
        ran));
    cache.MarkDocumentDirty();
    cache.UpdateAXForAllDocuments();
    EXPECT_TRUE(*ran);
  }


  void ExpectRealProvenanceBoundary(unsigned expected_units,
                                    wtf_size_t expected_items,
                                    bool exercise_full_capture,
                                    bool exercise_invalid_ranges = false) {
    auto& cache = GetAXObjectCache();
    auto ran = std::make_shared<bool>(false);
    cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> weak_cache,
           WeakPersistent<Node> target, unsigned expected_units,
           wtf_size_t expected_items, bool exercise_full_capture,
           bool exercise_invalid_ranges, std::shared_ptr<bool> ran) {
          ASSERT_NE(nullptr, weak_cache.Get());
          ASSERT_NE(nullptr, target.Get());
          ASSERT_TRUE(weak_cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          auto* source = DynamicTo<Text>(target.Get());
          ASSERT_NE(nullptr, source);
          ASSERT_EQ(expected_units, source->length());
          auto* layout = DynamicTo<LayoutText>(source->GetLayoutObject());
          ASSERT_NE(nullptr, layout);
          const LayoutObject* ancestor = layout->Parent();
          while (ancestor && !ancestor->IsLayoutBlock()) ancestor = ancestor->Parent();
          const auto* block = DynamicTo<LayoutBlockFlow>(ancestor);
          ASSERT_NE(nullptr, block);
          ASSERT_FALSE(block->NeedsCollectInlines());
          auto* data = block->GetInlineNodeData();
          ASSERT_NE(nullptr, data);
          ASSERT_TRUE(layout->HasValidInlineItems());
          // Assert REAL derived count before asking the identity getter.
          ASSERT_EQ(expected_items, layout->InlineItems().size());
          ASSERT_FALSE(layout->InlineItems().empty());
          const auto begin = layout->InlineItems().front().Index();
          const auto count = layout->InlineItems().size();
          const auto original_start = layout->InlineItems().front().StartOffset();
          const auto original_end = layout->InlineItems().back().EndOffset();
          ASSERT_EQ(expected_units, original_end - original_start);
          unsigned current = original_start;
          for (const auto& item : layout->InlineItems()) {
            ASSERT_EQ(layout, item->GetLayoutObject());
            ASSERT_EQ(InlineItem::kText, item->Type());
            ASSERT_EQ(TextItemType::kNormal, item->TextType());
            ASSERT_FALSE(item->IsGeneratedForLineBreak());
            ASSERT_EQ(current, item->StartOffset());
            ASSERT_GT(item->EndOffset(), current);
            current = item->EndOffset();
          }
          const bool supported = expected_units <= 4096 && expected_items <= 512;
          unsigned start = 111, end = 222;
          {
            SelectedSemanticReadScope observer(source->GetDocument());
            EXPECT_EQ(supported ? LayoutText::SemanticTextIdentity::kIdentity
                                : LayoutText::SemanticTextIdentity::kUnsupported,
                      layout->GetSemanticTextIdentity(*data, start, end));
            EXPECT_TRUE(observer.IsClean());
            EXPECT_EQ(0u, observer.ReadCounts().content);
            EXPECT_EQ(0u, observer.ReadCounts().forbidden);
          }
          EXPECT_EQ(supported ? original_start : 0u, start);
          EXPECT_EQ(supported ? original_end : 0u, end);
          if (exercise_full_capture) {
            // Only the all-ASCII wrapped-Ahem case exercises full capture. The
            // bidi item-count case tests producer state, not fragment budgets
            // or eligibility of this text for eventual browser extraction.
            if (supported) {
              InlineCursor cursor(*block->GetPhysicalFragment(0));
              cursor.MoveTo(*layout);
              unsigned fragments = 0;
              for (; cursor; cursor.MoveToNextForSameLayoutObject()) {
                ASSERT_LT(fragments++, 16u);
                ASSERT_FALSE(cursor.Current().RectInContainerFragment().IsEmpty());
              }
              ASSERT_GT(fragments, 1u);
            }
            AXObject* object = weak_cache->Get(source);
            ASSERT_NE(nullptr, object);
            SemanticBudgetV1 budget;
            SemanticAuditV1 audit;
            const auto result = ReadSelectedSemanticNodeV1(*object, budget, audit);
            if (supported) {
              EXPECT_EQ(SemanticDispositionV1::kAdmitted, result.disposition);
              EXPECT_EQ(expected_units, result.text.length());
              EXPECT_EQ(expected_units, budget.utf8_bytes);
              EXPECT_EQ(1u, audit.text_reads);
            } else {
              EXPECT_EQ(SemanticDispositionV1::kLimitExceeded, result.disposition);
              EXPECT_TRUE(result.text.empty());
              EXPECT_EQ(0u, budget.utf8_bytes);
              EXPECT_EQ(0u, audit.text_reads);
            }
            EXPECT_EQ(0u, audit.forbidden_reads);
          }
          if (exercise_invalid_ranges) {
            ASSERT_TRUE(supported);
            const wtf_size_t total = data->items.size();
            ASSERT_GT(total, 0u);
            ASSERT_LT(total, std::numeric_limits<wtf_size_t>::max());
            struct Range { wtf_size_t begin; wtf_size_t count; };
            const Range invalid[] = {
                {begin, 0}, {total, 0}, {total, 1}, {total + 1, 1},
                {begin, total - begin + 1}};
            for (const auto& range : invalid) {
              SCOPED_TRACE(range.begin);
              SCOPED_TRACE(range.count);
              // Fault injection/reassociation is test setup, deliberately
              // OUTSIDE any SelectedSemanticReadScope.
              layout->SetInlineItems(data, range.begin, range.count);
              EXPECT_FALSE(layout->HasValidInlineItems());
              start = 111;
              end = 222;
              {
                SelectedSemanticReadScope observer(source->GetDocument());
                EXPECT_EQ(LayoutText::SemanticTextIdentity::kUnavailable,
                          layout->GetSemanticTextIdentity(*data, start, end));
                EXPECT_EQ(0u, start);
                EXPECT_EQ(0u, end);
                EXPECT_TRUE(observer.IsClean());
                EXPECT_EQ(0u, observer.ReadCounts().content);
                EXPECT_EQ(0u, observer.ReadCounts().forbidden);
              }
              // Restore exact original association before the next case and
              // before leaving the frozen callback; no layout is prepared.
              layout->SetInlineItems(data, begin, count);
              ASSERT_TRUE(layout->HasValidInlineItems());
              EXPECT_EQ(LayoutText::SemanticTextIdentity::kIdentity,
                        layout->GetSemanticTextIdentity(*data, start, end));
              EXPECT_EQ(original_start, start);
              EXPECT_EQ(original_end, end);
              EXPECT_EQ(count, layout->InlineItems().size());
            }
          }
          *ran = true;
        }, WrapWeakPersistent(&cache), WrapWeakPersistent(TargetText()),
        expected_units, expected_items, exercise_full_capture,
        exercise_invalid_ranges, ran));
    cache.MarkDocumentDirty();
    cache.UpdateAXForAllDocuments();
    EXPECT_TRUE(*ran);
  }


  struct ReadOutcome {
    SemanticNodeResultV1 result;
    SemanticAuditV1 audit;
    SemanticBudgetV1 budget;
    bool callback_ran = false;
    bool has_inline_data = false;
    bool has_existing_mapping = false;
    bool needs_collect_inlines = false;
    bool had_ax_object = false;
    bool cached_hidden_via_style = false;
    bool missing_mapping_stayed_missing = false;
  };
  ReadOutcome RunSemantic(Node* target, SemanticBudgetV1 initial = {},
                          bool structure_only = false,
                          bool remove_mapping = false,
                          bool deliberate_bypass = false) {
    auto result = std::make_shared<ReadOutcome>();
    result->budget = initial;
    auto& cache = GetAXObjectCache();
    cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> weak_cache,
           WeakPersistent<Node> target, std::shared_ptr<ReadOutcome> result,
           bool structure_only, bool remove_mapping, bool deliberate_bypass) {
          ASSERT_NE(nullptr, weak_cache.Get());
          ASSERT_NE(nullptr, target.Get());
          ASSERT_TRUE(weak_cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          AXObject* object = weak_cache->Get(target.Get());
          result->callback_ran = true;
          result->had_ax_object = object != nullptr;
          if (auto* text_layout = DynamicTo<LayoutText>(target->GetLayoutObject())) {
            const auto* container = text_layout->Parent();
            while (container && !container->IsLayoutBlock())
              container = container->Parent();
            if (const auto* block = DynamicTo<LayoutBlockFlow>(container)) {
              result->needs_collect_inlines = block->NeedsCollectInlines();
              const auto* inline_data = block->GetInlineNodeData();
              result->has_inline_data = inline_data;
              result->has_existing_mapping = inline_data && inline_data->offset_mapping;
            }
          }
          if (structure_only) {
            if (object) {
              ASSERT_EQ(target.Get(), object->GetNode());
              ASSERT_FALSE(object->NeedsToUpdateCachedValues());
              result->cached_hidden_via_style =
                  static_cast<const AXObject&>(*object).IsHiddenViaStyle();
            }
            result->result.disposition = ClassifySelectedStructureV1(
                *target, *weak_cache, result->budget, result->audit);
            return;
          }
          ASSERT_NE(nullptr, object);
          InlineNodeData* data = nullptr;
          if (remove_mapping) {
            auto* layout = DynamicTo<LayoutText>(target->GetLayoutObject());
            ASSERT_NE(nullptr, layout);
            const auto* ancestor = layout->Parent();
            while (ancestor && !ancestor->IsLayoutBlock()) ancestor = ancestor->Parent();
            const auto* container = DynamicTo<LayoutBlockFlow>(ancestor);
            ASSERT_NE(nullptr, container);
            data = container->GetInlineNodeData();
            ASSERT_NE(nullptr, data);
            data->offset_mapping = nullptr;
          }
          if (deliberate_bypass) {
            SelectedSemanticReadScope outer(target->GetDocument());
            EXPECT_TRUE(To<Text>(target.Get())->data().empty());
            result->result = ReadSelectedSemanticNodeV1(
                *object, result->budget, result->audit);
            EXPECT_FALSE(outer.IsClean());
            EXPECT_EQ(1u, outer.ReadCounts().forbidden);
            EXPECT_EQ(0u, outer.ReadCounts().content);
          } else {
            result->result = ReadSelectedSemanticNodeV1(
                *object, result->budget, result->audit);
          }
          if (data) result->missing_mapping_stayed_missing = !data->offset_mapping;
        }, WrapWeakPersistent(&cache), WrapWeakPersistent(target), result,
        structure_only, remove_mapping, deliberate_bypass));
    cache.MarkDocumentDirty();
    cache.UpdateAXForAllDocuments();
    EXPECT_TRUE(result->callback_ran);
    return *result;
  }
  void ExpectSemanticFault(Node* target, SemanticFault fault) {
    auto ran = std::make_shared<bool>(false);
    auto& cache = GetAXObjectCache();
    cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> weak_cache,
           WeakPersistent<Node> target, SemanticFault fault,
           std::shared_ptr<bool> ran) {
          ASSERT_TRUE(weak_cache && target);
          ASSERT_TRUE(weak_cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          auto* object = weak_cache->Get(target.Get());
          ASSERT_NE(nullptr, object);
          auto* layout = DynamicTo<LayoutText>(target->GetLayoutObject());
          ASSERT_NE(nullptr, layout);
          const auto* ancestor = layout->Parent();
          while (ancestor && !ancestor->IsLayoutBlock()) ancestor = ancestor->Parent();
          const auto* block = DynamicTo<LayoutBlockFlow>(ancestor);
          ASSERT_NE(nullptr, block);
          auto* data = block->GetInlineNodeData();
          ASSERT_NE(nullptr, data);
          ASSERT_TRUE(layout->HasValidInlineItems());
          ASSERT_FALSE(layout->InlineItems().empty());
          const auto begin = layout->InlineItems().front().Index();
          const auto count = layout->InlineItems().size();
          *ran = true;
          SemanticBudgetV1 budget;
          SemanticAuditV1 audit;
          SemanticNodeResultV1 result;
          AXObject* dirty = nullptr;
          if (fault == SemanticFault::kDirtySourceAX ||
              fault == SemanticFault::kDirtyAncestorAX) {
            dirty = fault == SemanticFault::kDirtySourceAX
                ? object : weak_cache->Get(target->parentNode());
            ASSERT_NE(nullptr, dirty);
            ASSERT_FALSE(dirty->NeedsToUpdateCachedValues());
            SemanticAXCacheStateTestAccess::SetDirty(*dirty, true);
          }
          if (fault == SemanticFault::kInvalidateItems) layout->InvalidateInlineItems();
          if (fault == SemanticFault::kClearItems) layout->ClearInlineItems();
          if (fault == SemanticFault::kWrongData) {
            auto* unrelated = MakeGarbageCollected<InlineNodeData>();
            unsigned start = 123, end = 456;
            EXPECT_EQ(LayoutText::SemanticTextIdentity::kUnavailable,
                      layout->GetSemanticTextIdentity(*unrelated, start, end));
            EXPECT_EQ(0u, start);
            EXPECT_EQ(0u, end);
            result = ReadSelectedSemanticNodeV1(*object, budget, audit);
            EXPECT_EQ(SemanticDispositionV1::kAdmitted, result.disposition);
            EXPECT_EQ("Allowed", result.text);
            EXPECT_EQ(1u, audit.text_reads);
          } else if (fault == SemanticFault::kCompareDuringObserver) {
            {
              SelectedSemanticReadScope observer(target->GetDocument());
              // Deliberately invoke the ambient path while a service scope is
              // active. Its source getter must fail before materialization.
              layout->SetInlineItems(data, begin, count);
              EXPECT_FALSE(observer.IsClean());
              EXPECT_EQ(1u, observer.ReadCounts().forbidden);
              EXPECT_EQ(0u, observer.ReadCounts().content);
              unsigned start = 0, end = 0;
              EXPECT_NE(LayoutText::SemanticTextIdentity::kIdentity,
                        layout->GetSemanticTextIdentity(*data, start, end));
              result = ReadSelectedSemanticNodeV1(*object, budget, audit);
              EXPECT_EQ(SemanticDispositionV1::kNotReady, result.disposition);
              EXPECT_TRUE(result.text.empty());
              EXPECT_EQ(0u, audit.text_reads);
            }
            // Restore the actual association outside the observation interval.
            layout->SetInlineItems(data, begin, count);
          } else {
            result = ReadSelectedSemanticNodeV1(*object, budget, audit);
            EXPECT_EQ(SemanticDispositionV1::kNotReady, result.disposition);
            EXPECT_TRUE(result.text.empty());
            EXPECT_EQ(0u, audit.text_reads);
            if (dirty) {
              EXPECT_TRUE(dirty->NeedsToUpdateCachedValues());
              SemanticAXCacheStateTestAccess::SetDirty(*dirty, false);
            } else {
              EXPECT_FALSE(layout->HasValidInlineItems());
            }
          }
          EXPECT_EQ(0u, audit.forbidden_reads);
        }, WrapWeakPersistent(&cache), WrapWeakPersistent(target), fault, ran));
    cache.MarkDocumentDirty();
    cache.UpdateAXForAllDocuments();
    EXPECT_TRUE(*ran);
  }

  void ExpectRejected(const ReadOutcome& outcome,
                      SemanticDispositionV1 expected, unsigned content_reads = 0) {
    EXPECT_EQ(expected, outcome.result.disposition);
    EXPECT_TRUE(outcome.result.text.empty());
    EXPECT_EQ(content_reads, outcome.audit.text_reads);
    EXPECT_EQ(0u, outcome.audit.forbidden_reads);
  }
  Node* TargetText() { return GetElementById("target")->firstChild(); }
  bool ArmText(SelectedSemanticReadScope& scope, const Node& admitted,
               const CharacterData& source) {
    return scope.PermitText(admitted, source);
  }
  bool ArmLabel(SelectedSemanticReadScope& scope, const Node& admitted,
                const Element& owner) {
    const auto attributes = owner.AttributesWithoutUpdate();
    const auto* attribute = attributes.Find(html_names::kAriaLabelAttr);
    if (!attribute) return false;
    return scope.PermitAttribute(admitted, owner, static_cast<unsigned>(attribute - attributes.begin()),
                                SelectedSemanticReadScope::Purpose::kButtonLabel);
  }
  bool ArmHidden(SelectedSemanticReadScope& scope, const Element& owner,
                 unsigned index) {
    return scope.PermitAttribute(owner, owner, index,
                                SelectedSemanticReadScope::Purpose::kAriaHidden);
  }
  void SaturateCounts(SelectedSemanticReadScope& scope) {
    scope.root_->counts_.forbidden = std::numeric_limits<unsigned>::max();
  }
  void ExpectSemanticText(Node* target, const String& expected_text) {
    AXObjectCacheImpl& cache = GetAXObjectCache();
    using Result = std::optional<SemanticNodeResultV1>;
    auto result = std::make_shared<Result>();
    auto audit = std::make_shared<SemanticAuditV1>();
    cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> weak_cache,
           WeakPersistent<Node> target, std::shared_ptr<Result> result,
           std::shared_ptr<SemanticAuditV1> audit) {
          ASSERT_NE(nullptr, weak_cache.Get());
          ASSERT_NE(nullptr, target.Get());
          ASSERT_TRUE(weak_cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          AXObject* object = weak_cache->Get(target.Get());
          ASSERT_NE(nullptr, object);
          SemanticBudgetV1 budget;
          *result = ReadSelectedSemanticNodeV1(*object, budget, *audit);
        }, WrapWeakPersistent(&cache), WrapWeakPersistent(target), result, audit));
    cache.MarkDocumentDirty();
    cache.UpdateAXForAllDocuments();
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(SemanticDispositionV1::kAdmitted, result->value().disposition);
    EXPECT_EQ(expected_text, result->value().text);
    EXPECT_EQ(1u, audit->text_reads);
  }

  void ExpectSyntheticOverflow(Node* target, float available_width,
                               bool require_ellipsis = false) {
    // Ambient fixture witness before the measured capture. The default test
    // font is 1px; string length alone cannot establish real clipping.
    GetAXObjectCache();
    auto* text = DynamicTo<LayoutText>(target->GetLayoutObject());
    ASSERT_NE(nullptr, text);
    ASSERT_EQ(10.0f, text->StyleRef().ComputedFontSize());
    auto* block = DynamicTo<LayoutBlockFlow>(text->Parent());
    ASSERT_NE(nullptr, block);
    ASSERT_EQ(1u, block->PhysicalFragmentCount());
    const auto* items = block->GetPhysicalFragment(0)->Items();
    ASSERT_NE(nullptr, items);
    bool own_overflow = false, generated_ellipsis = false;
    for (const auto& item : items->Items()) {
      if (item.GetLayoutObject() == text && item.Type() == FragmentItem::kText)
        own_overflow |= item.RectInContainerFragment().Width().ToFloat() > available_width;
      generated_ellipsis |= item.IsGeneratedText();
    }
    ASSERT_TRUE(own_overflow);
    if (require_ellipsis) ASSERT_TRUE(generated_ellipsis);
  }

  void ExpectGeometry(const String& html, SemanticDispositionV1 expected) {
    SetBodyInnerHTML(html);
    ExpectCurrentGeometry(GetElementById("target"), expected);
  }

  void ExpectCurrentGeometry(Node* target, SemanticDispositionV1 expected,
                             bool invalidate_size = false) {
    // Test lifecycle setup precedes the measured service interval.
    ExpectFrozenGeometry(GetAXObjectCache(), target, expected, invalidate_size);
  }
};

// Transitions require a real WebFrameWidget during ambient lifecycle work.
// Follow the upstream AXViewTransitionTest fixture; capture still runs only at
// the actual frozen AX callback, with no lifecycle work inside measurement.
class SelectedSemanticPolicyTransitionTest : public testing::Test {
 protected:
  void SetUp() override {
    web_view_helper_ = std::make_unique<frame_test_helpers::WebViewHelper>();
    web_view_helper_->Initialize();
    web_view_helper_->Resize(gfx::Size(800, 600));
    ax_context_ = std::make_unique<AXContext>(GetDocument(),
                                            ui::kAXModeDefaultForTests);
  }

  void TearDown() override {
    ax_context_.reset();
    web_view_helper_.reset();
  }

  LocalFrame& GetFrame() {
    return *web_view_helper_->GetWebView()->MainFrameImpl()->GetFrame();
  }
  Document& GetDocument() { return *GetFrame().GetDocument(); }
  Element* GetElementById(const char* id) {
    return GetDocument().getElementById(AtomicString(id));
  }
  void SetBodyInnerHTML(const String& html) {
    GetDocument().body()->SetInnerHTMLWithoutTrustedTypes(html);
    UpdateLifecycle();
  }
  void UpdateLifecycle() {
    web_view_helper_->GetWebView()->MainFrameWidget()->UpdateAllLifecyclePhases(
        DocumentUpdateReason::kTest);
  }
  void ExpectCurrentGeometry(Node* target, SemanticDispositionV1 expected,
                             ExpectedTransitionState transition_state) {
    UpdateLifecycle();
    auto* cache = To<AXObjectCacheImpl>(GetDocument().ExistingAXObjectCache());
    ASSERT_NE(nullptr, cache);
    ExpectFrozenGeometry(*cache, target, expected, false, transition_state);
  }

  test::TaskEnvironment task_environment_;
  std::unique_ptr<frame_test_helpers::WebViewHelper> web_view_helper_;
  std::unique_ptr<AXContext> ax_context_;
};

TEST_F(SelectedSemanticPolicyTest, VisibleButtonIsAdmitted) {
  ExpectGeometry(R"HTML(
    <button id="target" aria-label="Allowed"
      style="width:80px;height:28px"></button>
  )HTML", SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, OwnZeroSizeCannotBorrowChildBounds) {
  ExpectGeometry(R"HTML(
    <button id="target" aria-label="Allowed"
      style="appearance:none;width:0;height:0;padding:0;border:0;overflow:visible">
      <span style="display:block;width:40px;height:20px">canary</span>
    </button>
  )HTML", SemanticDispositionV1::kOwnZeroSize);
}

TEST_F(SelectedSemanticPolicyTest, OwnZeroWidthCannotBorrowChildBounds) {
  ExpectGeometry(R"HTML(
    <button id="target" aria-label="Allowed"
      style="appearance:none;width:0;height:20px;padding:0;border:0;overflow:visible">
      <span style="display:block;width:40px;height:20px">canary</span>
    </button>
  )HTML", SemanticDispositionV1::kOwnZeroSize);
}

TEST_F(SelectedSemanticPolicyTest, OwnZeroHeightCannotBorrowChildBounds) {
  ExpectGeometry(R"HTML(
    <button id="target" aria-label="Allowed"
      style="appearance:none;width:40px;height:0;padding:0;border:0;overflow:visible">
      <span style="display:block;width:40px;height:20px">canary</span>
    </button>
  )HTML", SemanticDispositionV1::kOwnZeroSize);
}

TEST_F(SelectedSemanticPolicyTest, ZeroContentWidthWithPaddingIsNotOwnZeroSize) {
  ExpectGeometry(R"HTML(
    <button id="target" aria-label="Allowed"
      style="appearance:none;box-sizing:content-box;width:0;height:20px;padding:4px;border:1px solid"></button>
  )HTML", SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, OutsideEachViewportEdgeIsOffscreen) {
  const char* positions[] = {
      "left:-41px;top:100px", "left:801px;top:100px",
      "left:100px;top:-21px", "left:100px;top:601px"};
  for (const char* position : positions) {
    SCOPED_TRACE(position);
    ExpectGeometry(String("<style>html{overflow:hidden}body{margin:0}</style><button id=target "
                          "aria-label=Allowed style='appearance:none;"
                          "position:relative;display:block;width:40px;height:20px;"
                          "padding:0;border:0;") + position + "'></button>",
                   SemanticDispositionV1::kOffscreen);
  }
}

TEST_F(SelectedSemanticPolicyTest, ViewportEdgeContactHasNoPositiveArea) {
  const char* positions[] = {
      "left:-40px;top:100px", "left:800px;top:100px",
      "left:100px;top:-20px", "left:100px;top:600px"};
  for (const char* position : positions) {
    SCOPED_TRACE(position);
    ExpectGeometry(String("<style>html{overflow:hidden}body{margin:0}</style><button id=target "
                          "aria-label=Allowed style='appearance:none;"
                          "position:relative;display:block;width:40px;height:20px;"
                          "padding:0;border:0;") + position + "'></button>",
                   SemanticDispositionV1::kOffscreen);
  }
}

TEST_F(SelectedSemanticPolicyTest, PartialViewportIntersectionIsAdmitted) {
  ExpectGeometry(R"HTML(
    <style>html { overflow:hidden } body { margin:0 }</style>
    <button id=target aria-label=Allowed style="appearance:none;display:block;position:relative;
      left:-39px;top:100px;width:40px;height:20px;padding:0;border:0"></button>
  )HTML", SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, RectangularAncestorClipExcludesOwnBox) {
  ExpectGeometry(R"HTML(
    <div style="overflow:hidden;width:100px;height:40px">
      <button id=target aria-label=Allowed style="position:relative;
        left:120px;width:40px;height:20px"></button>
    </div>
  )HTML", SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, RectangularAncestorClipKeepsPositivePart) {
  ExpectGeometry(R"HTML(
    <div style="overflow:hidden;width:100px;height:40px">
      <button id=target aria-label=Allowed style="position:relative;
        left:90px;width:40px;height:20px"></button>
    </div>
  )HTML", SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, RotationIsUnsupported) {
  ExpectGeometry(R"HTML(
    <button id=target aria-label=Allowed
      style="width:80px;height:28px;transform:rotate(20deg)"></button>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, MaskIsUnsupported) {
  ExpectGeometry(R"HTML(
    <button id=target aria-label=Allowed style="width:80px;height:28px;
      mask-image:linear-gradient(black,transparent)"></button>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, RoundedAncestorClipIsUnsupported) {
  ExpectGeometry(R"HTML(
    <div style="overflow:hidden;width:100px;height:40px;border-radius:12px">
      <button id=target aria-label=Allowed style="width:80px;height:28px"></button>
    </div>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, AbsolutePositionIsInitiallyUnsupported) {
  ExpectGeometry(R"HTML(
    <button id=target aria-label=Allowed
      style="position:absolute;width:80px;height:28px"></button>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, InvalidCachedSizeIsNotPrepared) {
  SetBodyInnerHTML(R"HTML(
    <button id=target aria-label=Allowed style="width:80px;height:28px"></button>
  )HTML");
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kNotReady, true);
}

TEST_F(SelectedSemanticPolicyTest, PinchZoomIsUnsupported) {
  SetBodyInnerHTML("<button id=target aria-label=Allowed>Allowed</button>");
  auto& viewport = GetDocument().GetPage()->GetVisualViewport();
  viewport.SetScale(2);
  ASSERT_EQ(2, viewport.Scale());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, BlockFragmentationIsUnsupported) {
  ExpectGeometry(R"HTML(
    <div style="columns:2;column-fill:auto;height:80px;width:300px">
      <button id=target aria-label=Allowed style="width:80px;height:20px"></button>
      <div style="height:200px"></div>
    </div>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, VisibleOwnTextIsAdmitted) {
  SetBodyInnerHTML("<div id=target>Allowed text</div>");
  Node* text = GetElementById("target")->firstChild();
  ASSERT_TRUE(text && text->IsTextNode());
  ExpectCurrentGeometry(text, SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, SeparatedTextFragmentsCannotBorrowUnionVisibility) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <style>html { overflow:hidden } body { margin:0 }</style>
    <div id=target style="position:relative;top:-1100px;width:10px;
      font:10px/2000px Ahem;word-break:break-all">AA</div>
  )HTML");
  Node* text = GetElementById("target")->firstChild();
  ASSERT_TRUE(text && text->IsTextNode());
  const auto* layout_text = DynamicTo<LayoutText>(text->GetLayoutObject());
  ASSERT_NE(nullptr, layout_text);
  // Geometry-oracle setup is outside the measured policy callback.
  const auto* container = layout_text->FragmentItemsContainer();
  ASSERT_NE(nullptr, container);
  ASSERT_EQ(1u, container->PhysicalFragmentCount());
  InlineCursor cursor(*container);
  cursor.MoveTo(*layout_text);
  unsigned count = 0;
  PhysicalRect union_rect;
  const PhysicalRect viewport = GetLayoutView().ViewRect();
  for (; cursor; cursor.MoveToNextForSameLayoutObject()) {
    ASSERT_LT(count, 3u);
    const PhysicalRect local = cursor.Current().RectInContainerFragment();
    ASSERT_FALSE(local.IsEmpty());
    PhysicalRect mapped = container->LocalToAncestorRect(local, &GetLayoutView());
    union_rect.Unite(mapped);
    mapped.Intersect(viewport);
    EXPECT_TRUE(mapped.IsEmpty());
    ++count;
  }
  ASSERT_EQ(2u, count);
  union_rect.Intersect(viewport);
  ASSERT_FALSE(union_rect.IsEmpty());
  ExpectCurrentGeometry(text, SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, RootScrollIsAppliedExactlyOnce) {
  SetBodyInnerHTML(R"HTML(
    <style>body { margin:0 }</style>
    <button id=target aria-label=Allowed style="display:block;position:relative;
      top:450px;width:80px;height:28px"></button><div style="height:2000px"></div>
  )HTML");
  auto* area = GetDocument().View()->LayoutViewport();
  area->SetScrollOffset(ScrollOffset(0, 400),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  EXPECT_EQ(ScrollOffset(0, 400), area->GetScrollOffset());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, RootScrollExcludesAboveViewport) {
  SetBodyInnerHTML(R"HTML(
    <style>body { margin:0 }</style>
    <button id=target aria-label=Allowed style="display:block;position:relative;
      top:100px;width:80px;height:28px"></button><div style="height:2000px"></div>
  )HTML");
  auto* area = GetDocument().View()->LayoutViewport();
  area->SetScrollOffset(ScrollOffset(0, 400),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  EXPECT_EQ(ScrollOffset(0, 400), area->GetScrollOffset());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, NestedScrollKeepsVisibleOwnBox) {
  SetBodyInnerHTML(R"HTML(
    <div id=scroller style="overflow:scroll;width:150px;height:40px">
      <button id=target aria-label=Allowed style="display:block;position:relative;
        top:100px;width:80px;height:20px"></button><div style="height:500px"></div>
    </div>
  )HTML");
  auto* box = To<LayoutBox>(GetElementById("scroller")->GetLayoutObject());
  auto* area = box->GetScrollableArea();
  ASSERT_NE(nullptr, area);
  area->SetScrollOffset(ScrollOffset(0, 90),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  EXPECT_EQ(ScrollOffset(0, 90), area->GetScrollOffset());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, NestedScrollExcludesAboveClip) {
  SetBodyInnerHTML(R"HTML(
    <div id=scroller style="overflow:scroll;width:150px;height:40px">
      <button id=target aria-label=Allowed style="display:block;position:relative;
        top:10px;width:80px;height:20px"></button><div style="height:500px"></div>
    </div>
  )HTML");
  auto* box = To<LayoutBox>(GetElementById("scroller")->GetLayoutObject());
  auto* area = box->GetScrollableArea();
  ASSERT_NE(nullptr, area);
  area->SetScrollOffset(ScrollOffset(0, 90),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  EXPECT_EQ(ScrollOffset(0, 90), area->GetScrollOffset());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, EmptyTransitionSupplementIsAdmitted) {
  SetBodyInnerHTML("<button id=target aria-label=Allowed>Allowed</button>");
  GetDocument().GetViewTransitions();  // Explicit setup, outside capture.
  const auto* transitions = GetDocument().GetViewTransitionsIfExists();
  ASSERT_NE(nullptr, transitions);
  ASSERT_FALSE(transitions->HasPendingOrActiveTransitionsForSemanticCapture());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTransitionTest, DocumentTransitionIsUnsupported) {
  SetBodyInnerHTML("<button id=target aria-label=Allowed>Allowed</button>");
  ScriptState* script_state = ToScriptStateForMainWorld(&GetFrame());
  ScriptState::Scope scope(script_state);
  ASSERT_NE(nullptr, ViewTransitionSupplement::startViewTransition(
                         script_state, GetDocument(),
                         IGNORE_EXCEPTION_FOR_TESTING));
  ASSERT_TRUE(GetDocument().GetViewTransitionsIfExists()
                  ->HasPendingOrActiveTransitionsForSemanticCapture());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kUnsupportedGeometry,
                        ExpectedTransitionState::kDocument);
}

TEST_F(SelectedSemanticPolicyTransitionTest, ElementTransitionIsUnsupported) {
  SetBodyInnerHTML("<div id=scope><button id=target>Allowed</button></div>");
  ScriptState* script_state = ToScriptStateForMainWorld(&GetFrame());
  ScriptState::Scope scope(script_state);
  ASSERT_NE(nullptr, ScopedViewTransition::startViewTransition(
                         script_state, *GetElementById("scope"),
                         IGNORE_EXCEPTION_FOR_TESTING));
  auto* transitions = GetDocument().GetViewTransitionsIfExists();
  ASSERT_EQ(nullptr, transitions->GetTransition());
  ASSERT_TRUE(transitions->HasPendingOrActiveTransitionsForSemanticCapture());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kUnsupportedGeometry,
                        ExpectedTransitionState::kWithoutDocument);
}

TEST_F(SelectedSemanticPolicyTransitionTest, SkippedTransitionWithPendingCallbackIsUnsupported) {
  SetBodyInnerHTML("<button id=target aria-label=Allowed>Allowed</button>");
  ScriptState* script_state = ToScriptStateForMainWorld(&GetFrame());
  ScriptState::Scope scope(script_state);
  auto* transition = ViewTransitionSupplement::startViewTransition(
      script_state, GetDocument(), IGNORE_EXCEPTION_FOR_TESTING);
  ASSERT_NE(nullptr, transition);
  transition->skipTransition();
  auto* transitions = GetDocument().GetViewTransitionsIfExists();
  ASSERT_EQ(nullptr, transitions->GetTransition());
  ASSERT_TRUE(transitions->HasPendingOrActiveTransitionsForSemanticCapture());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kUnsupportedGeometry,
                        ExpectedTransitionState::kWithoutDocument);
}

TEST_F(SelectedSemanticPolicyTest, OwnTextContainerClipExcludesText) {
  SetBodyInnerHTML(R"HTML(
    <div id=target style="overflow:hidden;white-space:nowrap;text-indent:120px;
      width:100px;height:40px">Allowed text</div>
  )HTML");
  ExpectCurrentGeometry(GetElementById("target")->firstChild(),
                        SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, OwnTextContainerClipKeepsPositivePart) {
  SetBodyInnerHTML(R"HTML(
    <div id=target style="overflow:hidden;white-space:nowrap;text-indent:90px;
      width:100px;height:40px">Allowed text</div>
  )HTML");
  ExpectCurrentGeometry(GetElementById("target")->firstChild(),
                        SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, OwnTextContainerScrollMakesTextVisible) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <div id=target style="overflow:auto;white-space:nowrap;text-indent:120px;
      width:100px;height:40px;font:10px/20px Ahem">Allowed text</div>
  )HTML");
  auto* box = To<LayoutBox>(GetElementById("target")->GetLayoutObject());
  auto* area = box->GetScrollableArea();
  ASSERT_NE(nullptr, area);
  area->SetScrollOffset(ScrollOffset(100, 0),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  ASSERT_EQ(ScrollOffset(100, 0), area->GetScrollOffset());
  ExpectCurrentGeometry(GetElementById("target")->firstChild(),
                        SemanticDispositionV1::kAdmitted);
}

TEST_F(SelectedSemanticPolicyTest, OrdinaryScrollbarOccludesOwnBox) {
  ScopedMockOverlayScrollbars non_overlay_scrollbars(false);
  ASSERT_TRUE(non_overlay_scrollbars.IsSuccessful());
  SetBodyInnerHTML(R"HTML(
    <div id=scroller style="overflow:scroll;width:100px;height:100px">
      <button id=target aria-label=Allowed style="appearance:none;display:block;
        position:relative;left:95px;width:5px;height:20px;padding:0;border:0"></button>
      <div style="height:500px"></div>
    </div>
  )HTML");
  const auto* box = To<LayoutBox>(GetElementById("scroller")->GetLayoutObject());
  ASSERT_GT(box->GetScrollableArea()->VerticalScrollbarWidth(
                kIgnoreOverlayScrollbarSize), 5);
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, NonDefaultOpacityIsUnsupported) {
  ExpectGeometry(R"HTML(
    <button id=target aria-label=Allowed style="opacity:0;width:80px;height:28px"></button>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
  ExpectGeometry(R"HTML(
    <div style="opacity:0.5"><button id=target aria-label=Allowed
      style="width:80px;height:28px"></button></div>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, CurrentPaintAnimationsAreUnsupported) {
  struct Case { const char* property; const char* from; const char* to; };
  const Case cases[] = {{"opacity", "1", "0"},
                        {"filter", "none", "blur(10px)"},
                        {"backdrop-filter", "none", "blur(10px)"},
                        {"clip-path", "none", "circle(10px)"}};
  for (unsigned index = 0; index < 4; ++index) {
    const auto& test = cases[index];
    SCOPED_TRACE(test.property);
    SetBodyInnerHTML(String("<style>@keyframes effect {from {") + test.property +
                    ":" + test.from + "}to{" + test.property + ":" + test.to +
                    "}}</style><button id=target aria-label=Allowed style='width:80px;"
                    "height:28px;animation:effect 100s linear infinite paused'></button>");
    const auto& style = GetElementById("target")->GetLayoutObject()->StyleRef();
    const bool current[] = {style.HasCurrentOpacityAnimation(),
                            style.HasCurrentFilterAnimation(),
                            style.HasCurrentBackdropFilterAnimation(),
                            style.HasCurrentClipPathAnimation()};
    ASSERT_TRUE(current[index]);
    ExpectCurrentGeometry(GetElementById("target"),
                          SemanticDispositionV1::kUnsupportedGeometry);
  }
}

TEST_F(SelectedSemanticPolicyTest, AuthoredZoomIsUnsupported) {
  SetBodyInnerHTML(R"HTML(
    <button id=target aria-label=Allowed style="zoom:2;width:80px;height:28px"></button>
  )HTML");
  ASSERT_EQ(2, GetElementById("target")->GetLayoutObject()->StyleRef().EffectiveZoom());
  ExpectCurrentGeometry(GetElementById("target"),
                        SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, OverflowClipMarginIsUnsupported) {
  ExpectGeometry(R"HTML(
    <div style="overflow:clip;overflow-clip-margin:content-box 10px;
      padding:10%;width:100px;height:100px">
      <button id=target aria-label=Allowed style="width:80px;height:28px"></button>
    </div>
  )HTML", SemanticDispositionV1::kUnsupportedGeometry);
}

TEST_F(SelectedSemanticPolicyTest, RetainedTextSelfOverflowUsesCurrentFragments) {
  SetBodyInnerHTML("<div id=target>Allowed text</div>");
  ExpectFrozenGeometry(GetAXObjectCache(), GetElementById("target")->firstChild(),
                       SemanticDispositionV1::kAdmitted, false,
                       ExpectedTransitionState::kUnchecked,
                       ReadinessProbe::kRetainedTextSelfOverflow);
}

TEST_F(SelectedSemanticPolicyTest, DirtyTextAndContainerStateIsNotPrepared) {
  const ReadinessProbe probes[] = {
      ReadinessProbe::kTextChildOverflow, ReadinessProbe::kAncestorSelfOverflow,
      ReadinessProbe::kAncestorChildOverflow, ReadinessProbe::kTextPaint};
  for (const auto probe : probes) {
    SCOPED_TRACE(static_cast<unsigned>(probe));
    SetBodyInnerHTML("<div id=target>Allowed text</div>");
    ExpectFrozenGeometry(GetAXObjectCache(), GetElementById("target")->firstChild(),
                         SemanticDispositionV1::kNotReady, false,
                         ExpectedTransitionState::kUnchecked, probe);
  }
}

TEST_F(SelectedSemanticPolicyTest, IgnoredOrdinaryWrappersYieldOwnText) {
  SetBodyInnerHTML(R"HTML(
    <div role=presentation><span id=target>Allowed text</span></div>
  )HTML");
  Node* text = GetElementById("target")->firstChild();
  ASSERT_TRUE(text && text->IsTextNode());
  ExpectSemanticText(text, "Allowed text");
}

TEST_F(SelectedSemanticPolicyTest, DirectButtonLabelIsReadAfterAdmission) {
  SetBodyInnerHTML("<button id=target aria-label='Allowed label'></button>");
  ExpectSemanticText(GetElementById("target"), "Allowed label");
}

TEST_F(SelectedSemanticPolicyTest, ObserverDetectsExcludedTextBypass) {
  SetBodyInnerHTML("<div contenteditable><span id=target>Excluded canary</span></div>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  SemanticAuditV1 audit;
  {
    SelectedSemanticReadScope scope(GetDocument());
    // Deliberate bypass before classification: the approved seam never runs.
    const String& bypass = text->data();
    EXPECT_TRUE(bypass.empty());
    EXPECT_FALSE(scope.IsClean());
    EXPECT_EQ(SelectedSemanticReadScope::Violation::kUnlistedRead,
              scope.FirstViolation());
    EXPECT_EQ(1u, scope.ReadCounts().forbidden);
    EXPECT_EQ(0u, scope.ReadCounts().content);
    EXPECT_EQ(0u, audit.text_reads);
  }
  EXPECT_EQ("Excluded canary", text->data());
}

TEST_F(SelectedSemanticPolicyTest, ObserverDetectsRawAttributeBypass) {
  SetBodyInnerHTML("<button id=target aria-label='Excluded label'></button>");
  const auto* attribute = GetElementById("target")->AttributesWithoutUpdate().Find(
      html_names::kAriaLabelAttr);
  ASSERT_NE(nullptr, attribute);
  {
    SelectedSemanticReadScope scope(GetDocument());
    EXPECT_TRUE(attribute->Value().empty());
    EXPECT_FALSE(scope.IsClean());
    EXPECT_EQ(1u, scope.ReadCounts().forbidden);
    EXPECT_EQ(0u, scope.ReadCounts().structural);
  }
  EXPECT_EQ("Excluded label", attribute->Value());
}

TEST_F(SelectedSemanticPolicyTest, ObserverNestedScopeCannotResetViolation) {
  SetBodyInnerHTML("<span id=target>Excluded canary</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  SelectedSemanticReadScope outer(GetDocument());
  EXPECT_TRUE(text->data().empty());
  const auto first = outer.FirstViolation();
  {
    SelectedSemanticReadScope inner(GetDocument());
    EXPECT_FALSE(inner.IsClean());
    EXPECT_TRUE(text->data().empty());
    EXPECT_EQ(first, inner.FirstViolation());
  }
  EXPECT_FALSE(outer.IsClean());
  EXPECT_TRUE(text->data().empty());
  EXPECT_EQ(first, outer.FirstViolation());
  EXPECT_EQ(3u, outer.ReadCounts().forbidden);
}

TEST_F(SelectedSemanticPolicyTest, ObserverExactTextPermitIsOneUse) {
  SetBodyInnerHTML("<span id=target>Allowed text</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  SelectedSemanticReadScope scope(GetDocument());
  ASSERT_TRUE(ArmText(scope, *text, *text));
  EXPECT_EQ("Allowed text", text->data());
  EXPECT_TRUE(scope.IsClean());
  EXPECT_EQ(1u, scope.ReadCounts().content);
  EXPECT_TRUE(text->data().empty());
  EXPECT_FALSE(scope.IsClean());
  EXPECT_EQ(1u, scope.ReadCounts().forbidden);
  EXPECT_FALSE(ArmText(scope, *text, *text));
  EXPECT_TRUE(text->data().empty());
  EXPECT_EQ(1u, scope.ReadCounts().content);
}

TEST_F(SelectedSemanticPolicyTest, ObserverWrongTextSourceRevokesPermit) {
  SetBodyInnerHTML("<span id=target>Allowed</span><span id=other>Excluded</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  auto* other = To<Text>(GetElementById("other")->firstChild());
  SelectedSemanticReadScope scope(GetDocument());
  ASSERT_TRUE(ArmText(scope, *text, *text));
  EXPECT_TRUE(other->data().empty());
  EXPECT_EQ(SelectedSemanticReadScope::Violation::kWrongSource,
            scope.FirstViolation());
  EXPECT_TRUE(text->data().empty());
  EXPECT_FALSE(ArmText(scope, *text, *text));
  EXPECT_EQ(0u, scope.ReadCounts().content);
}

TEST_F(SelectedSemanticPolicyTest, ObserverWrongAdmittedTextCannotArm) {
  SetBodyInnerHTML("<span id=target>Excluded</span>");
  auto* owner = GetElementById("target");
  auto* text = To<Text>(owner->firstChild());
  SelectedSemanticReadScope scope(GetDocument());
  EXPECT_FALSE(ArmText(scope, *owner, *text));
  EXPECT_EQ(SelectedSemanticReadScope::Violation::kWrongSource,
            scope.FirstViolation());
  EXPECT_TRUE(text->data().empty());
}

TEST_F(SelectedSemanticPolicyTest, ObserverWrongDocumentCannotArm) {
  SetBodyInnerHTML("<span id=target>Excluded</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  auto other = std::make_unique<DummyPageHolder>(gfx::Size(800, 600));
  SelectedSemanticReadScope scope(other->GetDocument());
  EXPECT_FALSE(ArmText(scope, *text, *text));
  EXPECT_EQ(SelectedSemanticReadScope::Violation::kWrongDocument,
            scope.FirstViolation());
  EXPECT_TRUE(text->data().empty());
}

TEST_F(SelectedSemanticPolicyTest, ObserverExactLabelPermitAndWrongPurpose) {
  SetBodyInnerHTML("<button id=target aria-label='Allowed label'></button>");
  auto* owner = GetElementById("target");
  const auto attributes = owner->AttributesWithoutUpdate();
  const auto* label = attributes.Find(html_names::kAriaLabelAttr);
  {
    SelectedSemanticReadScope scope(GetDocument());
    ASSERT_TRUE(ArmLabel(scope, *owner, *owner));
    EXPECT_EQ("Allowed label", label->Value());
    EXPECT_EQ(1u, scope.ReadCounts().content);
    EXPECT_TRUE(label->Value().empty());
    EXPECT_FALSE(scope.IsClean());
  }
  {
    SelectedSemanticReadScope scope(GetDocument());
    EXPECT_FALSE(ArmHidden(scope, *owner, static_cast<unsigned>(label - attributes.begin())));
    EXPECT_EQ(SelectedSemanticReadScope::Violation::kInvalidPermit,
              scope.FirstViolation());
    EXPECT_TRUE(label->Value().empty());
    EXPECT_EQ(0u, scope.ReadCounts().structural);
  }
}

TEST_F(SelectedSemanticPolicyTest, ObserverStructuralReadsAreSeparate) {
  SetBodyInnerHTML("<div id=target aria-hidden=true></div>");
  auto* owner = GetElementById("target");
  const auto attributes = owner->AttributesWithoutUpdate();
  const auto* hidden = attributes.Find(html_names::kAriaHiddenAttr);
  SelectedSemanticReadScope scope(GetDocument());
  ASSERT_TRUE(ArmHidden(scope, *owner, static_cast<unsigned>(hidden - attributes.begin())));
  EXPECT_EQ("true", hidden->Value());
  EXPECT_TRUE(scope.IsClean());
  EXPECT_EQ(1u, scope.ReadCounts().structural);
  EXPECT_EQ(0u, scope.ReadCounts().content);
}

TEST_F(SelectedSemanticPolicyTest, ObserverSharedAttributeStillRequiresAdmittedOwner) {
  SetBodyInnerHTML("<button aria-label='Same'></button><button aria-label='Same'></button>");
  auto* first = To<Element>(GetDocument().body()->firstChild());
  auto* second = To<Element>(first->nextSibling());
  const auto* a = first->AttributesWithoutUpdate().Find(html_names::kAriaLabelAttr);
  const auto* b = second->AttributesWithoutUpdate().Find(html_names::kAriaLabelAttr);
  ASSERT_EQ(a, b);  // Real parser-deduplicated ShareableElementData.
  SelectedSemanticReadScope scope(GetDocument());
  EXPECT_FALSE(ArmLabel(scope, *first, *second));
  EXPECT_EQ(SelectedSemanticReadScope::Violation::kWrongSource,
            scope.FirstViolation());
  EXPECT_TRUE(b->Value().empty());
  EXPECT_EQ(0u, scope.ReadCounts().content);
}

TEST_F(SelectedSemanticPolicyTest, ObserverPendingPermitCannotCrossNestedScope) {
  SetBodyInnerHTML("<span id=target>Excluded</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  SelectedSemanticReadScope outer(GetDocument());
  ASSERT_TRUE(ArmText(outer, *text, *text));
  {
    SelectedSemanticReadScope inner(GetDocument());
    EXPECT_EQ(SelectedSemanticReadScope::Violation::kNestedPendingPermit,
              inner.FirstViolation());
    EXPECT_TRUE(text->data().empty());
  }
  EXPECT_FALSE(outer.IsClean());
  EXPECT_FALSE(ArmText(outer, *text, *text));
  EXPECT_TRUE(text->data().empty());
}

TEST_F(SelectedSemanticPolicyTest, ObserverNestedDifferentDocumentPoisonsOuter) {
  auto other = std::make_unique<DummyPageHolder>(gfx::Size(800, 600));
  SelectedSemanticReadScope outer(GetDocument());
  {
    SelectedSemanticReadScope inner(other->GetDocument());
    EXPECT_FALSE(inner.IsClean());
    EXPECT_EQ(SelectedSemanticReadScope::Violation::kWrongDocument,
              inner.FirstViolation());
  }
  EXPECT_FALSE(outer.IsClean());
}

TEST_F(SelectedSemanticPolicyTest, ObserverUnusedNestedPermitExpires) {
  SetBodyInnerHTML("<span id=target>Allowed</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  {
    SelectedSemanticReadScope outer(GetDocument());
    {
      SelectedSemanticReadScope inner(GetDocument());
      ASSERT_TRUE(ArmText(inner, *text, *text));
    }
    EXPECT_EQ(SelectedSemanticReadScope::Violation::kExpiredPermit,
              outer.FirstViolation());
    EXPECT_TRUE(text->data().empty());
  }
  SelectedSemanticReadScope independent(GetDocument());
  EXPECT_TRUE(independent.IsClean());
  EXPECT_EQ(0u, independent.ReadCounts().forbidden);
  ASSERT_TRUE(ArmText(independent, *text, *text));
  EXPECT_EQ("Allowed", text->data());
}

TEST_F(SelectedSemanticPolicyTest, ObserverForbiddenCounterSaturates) {
  SetBodyInnerHTML("<span id=target>Excluded</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  SelectedSemanticReadScope scope(GetDocument());
  SaturateCounts(scope);
  EXPECT_TRUE(text->data().empty());
  EXPECT_EQ(std::numeric_limits<unsigned>::max(), scope.ReadCounts().forbidden);
}

TEST_F(SelectedSemanticPolicyTest, ObserverWrongPrimitiveConsumesNoContent) {
  SetBodyInnerHTML("<button aria-label=Excluded></button><span id=target>Allowed</span>");
  auto* text = To<Text>(GetElementById("target")->firstChild());
  const auto* owner = To<Element>(GetDocument().body()->firstChild());
  const auto* label = owner->AttributesWithoutUpdate().Find(html_names::kAriaLabelAttr);
  SelectedSemanticReadScope scope(GetDocument());
  ASSERT_TRUE(ArmText(scope, *text, *text));
  EXPECT_TRUE(label->Value().empty());
  EXPECT_EQ(SelectedSemanticReadScope::Violation::kUnlistedRead,
            scope.FirstViolation());
  EXPECT_TRUE(text->data().empty());
  EXPECT_EQ(0u, scope.ReadCounts().content);
  EXPECT_EQ(2u, scope.ReadCounts().forbidden);
}

TEST_F(SelectedSemanticPolicyTest, ObserverWrongAttributeCannotConsumeLabelPermit) {
  SetBodyInnerHTML("<button id=target title=Excluded aria-label=Allowed></button>");
  auto* owner = GetElementById("target");
  const auto attributes = owner->AttributesWithoutUpdate();
  const auto* title = attributes.Find(html_names::kTitleAttr);
  const auto* label = attributes.Find(html_names::kAriaLabelAttr);
  SelectedSemanticReadScope scope(GetDocument());
  ASSERT_TRUE(ArmLabel(scope, *owner, *owner));
  EXPECT_TRUE(title->Value().empty());
  EXPECT_EQ(SelectedSemanticReadScope::Violation::kWrongSource,
            scope.FirstViolation());
  EXPECT_TRUE(label->Value().empty());
  EXPECT_EQ(0u, scope.ReadCounts().content);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsEditableAncestorThroughFalseOverride) {
  SetBodyInnerHTML("<div contenteditable=true><div contenteditable=false><span id=target>Excluded</span></div></div>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsAriaDisabledAncestorThroughFalseOverride) {
  SetBodyInnerHTML("<div aria-disabled=true><div aria-disabled=false><span id=target>Excluded</span></div></div>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsDisabledFieldsetWithoutFormGetter) {
  SetBodyInnerHTML("<fieldset disabled><legend><span id=target>Excluded</span></legend></fieldset>");
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsAriaHiddenSourceWithoutNameReads) {
  SetBodyInnerHTML("<div aria-hidden=true title='Excluded title'><span id=target>Excluded</span></div><p>Allowed neighbor</p>");
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsInertSource) {
  SetBodyInnerHTML("<div inert><span id=target>Excluded</span></div><p>Allowed neighbor</p>");
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, DisplayNoneRetainedAXSourceDoesNotPrepareStyle) {
  SetBodyInnerHTML("<div style='display:none'><span id=target>Excluded</span></div><p id=neighbor>Allowed neighbor</p>");
  ASSERT_EQ(nullptr, GetElementById("target")->GetComputedStyle());
  const auto outcome = RunSemantic(TargetText(), {}, true);
  EXPECT_TRUE(outcome.had_ax_object);
  EXPECT_TRUE(outcome.cached_hidden_via_style);
  ExpectRejected(outcome, SemanticDispositionV1::kForbiddenAncestor);
  EXPECT_EQ(nullptr, TargetText()->GetLayoutObject());
  EXPECT_EQ(nullptr, GetElementById("target")->GetComputedStyle());
  const auto neighbor = RunSemantic(GetElementById("neighbor")->firstChild());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, neighbor.result.disposition);
  EXPECT_EQ("Allowed neighbor", neighbor.result.text);
  EXPECT_EQ(1u, neighbor.audit.text_reads);
  EXPECT_EQ(0u, neighbor.audit.forbidden_reads);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsVisibilityHidden) {
  SetBodyInnerHTML("<div style='visibility:hidden'><span id=target>Excluded</span></div><p>Allowed neighbor</p>");
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsAllInputTypesWithoutValues) {
  for (const auto* type : {"text", "password", "checkbox"}) {
    SetBodyInnerHTML(String("<input id=target type='") + type +
                    "' value='Excluded value' aria-label='Excluded label'>");
    ExpectRejected(RunSemantic(GetElementById("target"), {}, true),
                   SemanticDispositionV1::kForbiddenAncestor);
  }
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsContentNamedButtonText) {
  SetBodyInnerHTML("<button><span id=target>Excluded button contents</span></button>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsAriaRemappedButtonSource) {
  SetBodyInnerHTML("<button id=target role=group aria-label='Excluded label'></button>");
  ExpectRejected(RunSemantic(GetElementById("target")), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, StructureTraversesFixtureGroupWithoutReadingItsLabel) {
  SetBodyInnerHTML("<div role=group aria-label='Excluded group label'><p id=target>Allowed anchor</p></div>");
  const auto outcome = RunSemantic(TargetText());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
  EXPECT_EQ("Allowed anchor", outcome.result.text);
  EXPECT_EQ(1u, outcome.audit.text_reads);
  EXPECT_EQ(0u, outcome.audit.structural_reads);
  EXPECT_EQ(0u, outcome.audit.forbidden_reads);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsUnknownAndTextboxRoles) {
  for (const auto* role : {"textbox", "unknown-role", "img"}) {
    SetBodyInnerHTML(String("<div role='") + role + "'><span id=target>Excluded</span></div>");
    ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
  }
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsCustomElementBeforeInternals) {
  SetBodyInnerHTML("<custom-wrapper><span id=target>Excluded</span></custom-wrapper>");
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, StructureBoundsStructuralToken) {
  SetBodyInnerHTML("<div aria-disabled=123456789012345><span id=target>Excluded</span></div>");
  const auto outcome = RunSemantic(TargetText());
  ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded);
  EXPECT_EQ(1u, outcome.audit.structural_reads);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsUnknownStructuralToken) {
  SetBodyInnerHTML("<div aria-disabled=unknown><span id=target>Excluded</span></div>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, AbsentOffsetMappingDoesNotRequirePreparation) {
  SetBodyInnerHTML("<p id=target>Allowed text</p>");
  const auto outcome = RunSemantic(TargetText(), {}, false, true);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
  EXPECT_EQ("Allowed text", outcome.result.text);
  EXPECT_EQ(1u, outcome.audit.text_reads);
  EXPECT_EQ(0u, outcome.audit.forbidden_reads);
  EXPECT_TRUE(outcome.missing_mapping_stayed_missing);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsCollapsedWhitespace) {
  SetBodyInnerHTML("<p id=target>One   two</p>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsTransformedText) {
  SetBodyInnerHTML("<p id=target style='text-transform:uppercase'>Excluded</p>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsSecureText) {
  SetBodyInnerHTML("<p id=target style='-webkit-text-security:disc'>Excluded</p>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kForbiddenAncestor);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsFirstLetterSplit) {
  SetBodyInnerHTML("<style>p::first-letter {font-size:30px}</style><p id=target>Excluded text</p>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsEllipsis) {
  LoadAhem();
  SetBodyInnerHTML("<p id=target style='font:10px/12px Ahem;width:20px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis'>Excluded long text</p>");
  ExpectSyntheticOverflow(TargetText(), 20 , true);
  ASSERT_FALSE(HasFatalFailure());
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsPartiallyClippedOwnContainer) {
  LoadAhem();
  SetBodyInnerHTML("<p id=target style='font:10px/12px Ahem;width:20px;white-space:pre;overflow:hidden'>Excluded long text</p>");
  ExpectSyntheticOverflow(TargetText(), 20);
  ASSERT_FALSE(HasFatalFailure());
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsPartiallyClippedAncestor) {
  LoadAhem();
  SetBodyInnerHTML("<div style='width:20px;overflow:hidden'><p id=target style='font:10px/12px Ahem;width:200px;white-space:pre'>Excluded long text</p></div>");
  ExpectSyntheticOverflow(TargetText(), 20);
  ASSERT_FALSE(HasFatalFailure());
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageRejectsViewportTail) {
  LoadAhem();
  SetBodyInnerHTML("<p id=target style='font:10px/12px Ahem;position:relative;left:770px;white-space:pre'>Excluded long text</p>");
  ExpectSyntheticOverflow(TargetText(), 22);
  ASSERT_FALSE(HasFatalFailure());
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kOffscreen);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageAdmitsWrappedPreservedText) {
  SetBodyInnerHTML("<p id=target style='width:100px;white-space:pre-wrap;font:13px/19px Arial'>Allowed wrapped text across several lines</p>");
  const auto outcome = RunSemantic(TargetText());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
  EXPECT_EQ("Allowed wrapped text across several lines", outcome.result.text);
  EXPECT_EQ(1u, outcome.audit.text_reads);
  EXPECT_EQ(0u, outcome.audit.forbidden_reads);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageDoesNotReadExcludedNeighborMappingText) {
  SetBodyInnerHTML("<p><span id=target>Allowed</span><span contenteditable=true>Excluded neighboring text</span></p>");
  const auto outcome = RunSemantic(TargetText());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
  EXPECT_EQ("Allowed", outcome.result.text);
  EXPECT_EQ(1u, outcome.audit.text_reads);
  EXPECT_EQ(0u, outcome.audit.forbidden_reads);
}

TEST_F(SelectedSemanticPolicyTest, ObserverBypassPoisonsActualReadEntry) {
  SetBodyInnerHTML("<div contenteditable><span id=target>Excluded</span></div>");
  const auto outcome = RunSemantic(TargetText(), {}, false, false, true);
  ExpectRejected(outcome, SemanticDispositionV1::kNotReady);
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageExactRightClipAndOneGridUnitLoss) {
  LoadAhem();
  for (const auto* width : {"100", "99.984375"}) {
    SetBodyInnerHTML(String("<p id=target style='margin:0;width:") + width +
                    "px;overflow:hidden;font:10px/10px Ahem'>XXXXXXXXXX</p>");
    const auto outcome = RunSemantic(TargetText());
    if (String(width) == "100") {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ("XXXXXXXXXX", outcome.result.text);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kOffscreen);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageExactBottomClipAndOneGridUnitLoss) {
  LoadAhem();
  for (const auto* height : {"10", "9.984375"}) {
    SetBodyInnerHTML(String("<p id=target style='margin:0;width:100px;height:") + height +
                    "px;overflow:hidden;font:10px/10px Ahem'>XXXXXXXXXX</p>");
    const auto outcome = RunSemantic(TargetText());
    if (String(height) == "10") {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kOffscreen);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageOneGridUnitLossAtAncestorLeftOrTop) {
  LoadAhem();
  for (const auto* position : {"left", "top"}) {
    SetBodyInnerHTML(String("<div style='width:100px;height:10px;overflow:hidden'><p id=target style='margin:0;width:100px;position:relative;") +
                    position + ":-0.015625px;font:10px/10px Ahem'>XXXXXXXXXX</p></div>");
    ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kOffscreen);
  }
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageExactRootClipAndOneGridUnitLoss) {
  LoadAhem();
  for (const auto* left : {"700", "700.015625"}) {
    SetBodyInnerHTML(String("<style>body{margin:0}</style><p id=target style='margin:0;width:100px;position:relative;left:") +
                    left + "px;font:10px/10px Ahem'>XXXXXXXXXX</p>");
    const auto outcome = RunSemantic(TargetText());
    if (String(left) == "700") {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kOffscreen);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, FullCoveragePrecisionEnvelopeBoundary) {
  LoadAhem();
  for (const auto* left : {"32718", "32718.015625"}) {
    SetBodyInnerHTML(String("<style>body{margin:0}</style><div style='position:relative;left:-32718px'><p id=target style='margin:0;width:100px;position:relative;left:") +
                    left + "px;font:10px/10px Ahem'>XXXXXXXXXX</p></div>");
    const auto outcome = RunSemantic(TargetText());
    if (String(left) == "32718") {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kUnsupportedGeometry);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentUtf8PerStringBoundary) {
  for (unsigned length : {4096u, 4097u}) {
    SetBodyInnerHTML("<button id=target></button>");
    GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
        AtomicString(String::FromUtf8(std::string(length, 'x'))));
    const auto outcome = RunSemantic(GetElementById("target"));
    EXPECT_EQ(1u, outcome.audit.text_reads);
    if (length == 4096) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ(4096u, outcome.result.text.length());
      EXPECT_EQ(4096u, outcome.budget.utf8_bytes);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded, 1);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentUtf8MultibyteBoundary) {
  for (unsigned count : {2048u, 2049u}) {
    SetBodyInnerHTML("<button id=target></button>");
    StringBuilder label;
    for (unsigned i = 0; i < count; ++i) label.Append(UChar(0x00e9));
    GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
                                         label.ToAtomicString());
    const auto outcome = RunSemantic(GetElementById("target"));
    if (count == 2048) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ(4096u, outcome.budget.utf8_bytes);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded, 1);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentTotalUtf8Boundary) {
  SetBodyInnerHTML("<button id=target aria-label=Allowed></button>");
  for (unsigned used : {65529u, 65530u, 65537u}) {
    SemanticBudgetV1 budget;
    budget.utf8_bytes = used;
    const auto outcome = RunSemantic(GetElementById("target"), budget);
    if (used == 65529) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ(65536u, outcome.budget.utf8_bytes);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded, 1);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentNodeFragmentAndRelationBudgetBoundaries) {
  SetBodyInnerHTML("<button id=target aria-label=Allowed></button>");
  const auto baseline = RunSemantic(GetElementById("target"));
  ASSERT_EQ(SemanticDispositionV1::kAdmitted, baseline.result.disposition);
  for (unsigned extra : {0u, 1u}) {
    SemanticBudgetV1 nodes;
    nodes.nodes = 256 - baseline.budget.nodes + extra;
    const auto n = RunSemantic(GetElementById("target"), nodes);
    SemanticBudgetV1 fragments;
    fragments.fragments = 511 + extra;
    const auto f = RunSemantic(GetElementById("target"), fragments);
    SemanticBudgetV1 steps;
    steps.relation_steps = 4096 - baseline.budget.relation_steps + extra;
    const auto r = RunSemantic(GetElementById("target"), steps);
    if (extra == 0) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, n.result.disposition);
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, f.result.disposition);
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, r.result.disposition);
      EXPECT_EQ(256u, n.budget.nodes);
      EXPECT_EQ(512u, f.budget.fragments);
      EXPECT_EQ(4096u, r.budget.relation_steps);
    } else {
      ExpectRejected(n, SemanticDispositionV1::kLimitExceeded);
      ExpectRejected(f, SemanticDispositionV1::kLimitExceeded);
      ExpectRejected(r, SemanticDispositionV1::kLimitExceeded);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentAncestorDepthBoundary) {
  for (unsigned wrappers : {60u, 61u}) {
    StringBuilder html;
    for (unsigned i = 0; i < wrappers; ++i) html.Append("<div>");
    html.Append("<button id=target aria-label=Allowed></button>");
    for (unsigned i = 0; i < wrappers; ++i) html.Append("</div>");
    SetBodyInnerHTML(html.ToString());
    const auto outcome = RunSemantic(GetElementById("target"));
    // button + wrappers + body + html + document = 64 or 65.
    if (wrappers == 60) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ(64u, outcome.budget.depth);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentAttributeCountBoundary) {
  for (unsigned count : {256u, 257u}) {
    StringBuilder html;
    html.Append("<button id=target aria-label=Allowed");
    for (unsigned i = 2; i < count; ++i) {
      html.Append(" data-");
      html.AppendNumber(i);
      html.Append("=Excluded");
    }
    html.Append("></button>");
    SetBodyInnerHTML(html.ToString());
    const auto outcome = RunSemantic(GetElementById("target"));
    if (count == 256) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ(1u, outcome.audit.text_reads);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded);
    }
  }
}

TEST_F(SelectedSemanticPolicyTest, FullCoverageFractionalScrollGridAndSubgrid) {
  ScopedFractionalScrollOffsetsForTest fractional_offsets(true);
  LoadAhem();
  for (float scroll : {0.5f, 0.001f}) {
    SetBodyInnerHTML("<div id=scroller style='width:200px;height:40px;overflow:hidden'><p id=target style='margin:0 0 0 100px;width:100px;font:10px/10px Ahem'>XXXXXXXXXX</p><div style='width:400px;height:1px'></div></div>");
    auto* area = To<LayoutBox>(GetElementById("scroller")->GetLayoutObject())
                     ->GetScrollableArea();
    ASSERT_NE(nullptr, area);
    area->SetScrollOffset(ScrollOffset(scroll, 0),
                         mojom::blink::ScrollType::kProgrammatic,
                         cc::ScrollSourceType::kAbsoluteScroll,
                         mojom::blink::ScrollBehavior::kInstant);
    ASSERT_EQ(scroll, area->GetScrollOffset().x());
    const auto outcome = RunSemantic(TargetText());
    if (scroll == 0.5f) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
      EXPECT_EQ("XXXXXXXXXX", outcome.result.text);
    } else {
      ExpectRejected(outcome, SemanticDispositionV1::kUnsupportedGeometry);
    }
    EXPECT_EQ(scroll, area->GetScrollOffset().x());
  }
}

TEST_F(SelectedSemanticPolicyTest, ContentInvalidUnicodeRejectsWholeLabel) {
  SetBodyInnerHTML("<button id=target></button>");
  StringBuilder label;
  label.Append("Allowed prefix");
  label.Append(UChar(0xd800));
  GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
                                       label.ToAtomicString());
  const auto outcome = RunSemantic(GetElementById("target"));
  ExpectRejected(outcome, SemanticDispositionV1::kUnsupportedText, 1);
  EXPECT_EQ(0u, outcome.budget.utf8_bytes);
}

TEST_F(SelectedSemanticPolicyTest, ContentOversizedTextRejectedBeforeMaterialization) {
  SetBodyInnerHTML("<p id=target></p>");
  auto* text = Text::Create(GetDocument(), String::FromUtf8(std::string(4097, 'x')));
  GetElementById("target")->AppendChild(text);
  text->MakeParkable();
  const auto outcome = RunSemantic(text);
  ExpectRejected(outcome, SemanticDispositionV1::kLimitExceeded);
  EXPECT_EQ(0u, outcome.budget.utf8_bytes);
}

TEST_F(SelectedSemanticPolicyTest, OrdinaryTextUsesProvenanceWithoutOffsetMapping) {
  SetBodyInnerHTML("<p id=target>Allowed text</p>");
  const auto outcome = RunSemantic(TargetText());
  EXPECT_TRUE(outcome.had_ax_object);
  EXPECT_TRUE(outcome.has_inline_data);
  EXPECT_FALSE(outcome.needs_collect_inlines);
  EXPECT_FALSE(outcome.has_existing_mapping);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, outcome.result.disposition);
  EXPECT_EQ("Allowed text", outcome.result.text);
}


TEST_F(SelectedSemanticPolicyTest, ProvenanceRejectsEqualLengthWhitespaceSubstitutions) {
  for (const char* value : {"A\tB", "A\nB", "A\rB", "A  B"}) {
    SetBodyInnerHTML("<p id=target></p>");
    GetElementById("target")->AppendChild(Text::Create(GetDocument(), String(value)));
    ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
  }
  SetBodyInnerHTML("<p id=target>A B</p>");
  ExpectSemanticText(TargetText(), "A B");
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceRejectsPreservedControlItems) {
  SetBodyInnerHTML("<p id=target style='white-space:pre'>A\tB</p>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceRejectsFirstLineVariant) {
  SetBodyInnerHTML("<style>#target::first-line{font-size:30px}</style><p id=target>Excluded</p>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceSameLengthReplacementIsRecomputed) {
  SetBodyInnerHTML("<p id=target>A B</p>");
  ExpectSemanticText(TargetText(), "A B");
  auto* text = To<Text>(TargetText());
  text->setData("A\tB");
  ExpectRejected(RunSemantic(text), SemanticDispositionV1::kUnsupportedText);
  text->setData("C D");
  ExpectSemanticText(text, "C D");
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceIncrementalInsertionAndDeletionAreRecomputed) {
  SetBodyInnerHTML("<p id=target>Allowed text</p>");
  auto* text = To<Text>(TargetText());
  ExpectSemanticText(text, "Allowed text");
  text->insertData(8, "new ", ASSERT_NO_EXCEPTION);
  ASSERT_EQ("Allowed new text", text->data());
  ExpectSemanticText(text, "Allowed new text");
  text->deleteData(8, 4, ASSERT_NO_EXCEPTION);
  ASSERT_EQ("Allowed text", text->data());
  ExpectSemanticText(text, "Allowed text");
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceNeighborCollapseIsRecomputed) {
  SetBodyInnerHTML("<div><span id=before>A</span><span id=target> B</span></div>");
  ExpectSemanticText(TargetText(), " B");
  To<Text>(GetElementById("before")->firstChild())->setData("A ");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
  To<Text>(GetElementById("before")->firstChild())->setData("A");
  ExpectSemanticText(TargetText(), " B");
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceReparentAndWhitespaceStyleAreRecomputed) {
  SetBodyInnerHTML("<div id=preserved style='white-space:pre'><span id=target>A  B</span></div><div id=normal></div>");
  ExpectSemanticText(TargetText(), "A  B");
  auto* target = GetElementById("target");
  GetElementById("normal")->AppendChild(target);
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kUnsupportedText);
  GetElementById("normal")->setAttribute(html_names::kStyleAttr, AtomicString("white-space:pre"));
  ExpectSemanticText(TargetText(), "A  B");
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsRealShadowAndSlottedSources) {
  SetBodyInnerHTML("<div id=host><span id=target slot=content>Excluded light</span></div><p id=neighbor>Allowed neighbor</p>");
  auto& shadow = GetElementById("host")->AttachShadowRootForTesting(ShadowRootMode::kOpen);
  shadow.SetInnerHTMLWithoutTrustedTypes("<slot name=content></slot><span id=inside>Excluded shadow</span>");
  UpdateAllLifecyclePhasesForTest();
  auto* inside = shadow.getElementById(AtomicString("inside"));
  ASSERT_TRUE(inside && inside->firstChild());
  ASSERT_TRUE(inside->firstChild()->IsInShadowTree());
  ExpectRejected(RunSemantic(inside->firstChild(), {}, true), SemanticDispositionV1::kUnsupportedText);
  ASSERT_FALSE(TargetText()->IsInShadowTree());
  ASSERT_EQ(GetElementById("host"), GetElementById("target")->parentNode());
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kUnsupportedText);
  ExpectSemanticText(GetElementById("neighbor")->firstChild(), "Allowed neighbor");
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsSlotOutsideShadow) {
  SetBodyInnerHTML("<slot><span id=target>Excluded fallback</span></slot>");
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kUnsupportedText);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsRealPseudoSource) {
  SetBodyInnerHTML("<style>#target::before{content:'Excluded generated'}</style><p id=target>Allowed own text</p>");
  UpdateAllLifecyclePhasesForTest();
  auto* pseudo = GetElementById("target")->GetPseudoElement(kPseudoIdBefore);
  ASSERT_NE(nullptr, pseudo);
  ASSERT_TRUE(pseudo->IsPseudoElement());
  ExpectRejected(RunSemantic(pseudo, {}, true), SemanticDispositionV1::kUnsupportedText);
  ExpectSemanticText(TargetText(), "Allowed own text");
}

TEST_F(SelectedSemanticPolicyTest, StructureAbsentScriptSourceRemainsAbsent) {
  SetBodyInnerHTML("<script type=application/json id=target>Excluded</script><p id=neighbor>Allowed neighbor</p>");
  const auto outcome = RunSemantic(TargetText(), {}, true);
  EXPECT_FALSE(outcome.had_ax_object);
  ExpectRejected(outcome, SemanticDispositionV1::kForbiddenAncestor);
  ExpectSemanticText(GetElementById("neighbor")->firstChild(), "Allowed neighbor");
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsRealModalExcludedSource) {
  SetBodyInnerHTML("<dialog id=modal><button>Inside</button></dialog><p id=target>Excluded outside</p>");
  auto* dialog = To<HTMLDialogElement>(GetElementById("modal"));
  dialog->showModal(ASSERT_NO_EXCEPTION);
  UpdateAllLifecyclePhasesForTest();
  ASSERT_EQ(dialog, GetDocument().ActiveModalDialog());
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kForbiddenAncestor);
  EXPECT_EQ(dialog, GetDocument().ActiveModalDialog());
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsCachedAriaModalExcludedSource) {
  GetDocument().GetSettings()->SetAriaModalPrunesAXTree(true);
  SetBodyInnerHTML("<div id=modal role=dialog aria-modal=true><button id=focus>Inside</button></div><p id=target>Excluded outside</p>");
  GetElementById("focus")->Focus();
  UpdateAllLifecyclePhasesForTest();
  ASSERT_EQ(GetElementById("modal"), GetAXObjectCache().GetActiveAriaModalDialog());
  ExpectRejected(RunSemantic(TargetText(), {}, true), SemanticDispositionV1::kForbiddenAncestor);
  GetDocument().GetSettings()->SetAriaModalPrunesAXTree(false);
}

TEST_F(SelectedSemanticPolicyTest, StructureRejectsAriaModalPresenceWithoutValueRead) {
  SetBodyInnerHTML("<div aria-modal=false><span id=target>Excluded</span></div>");
  const auto outcome = RunSemantic(TargetText(), {}, true);
  ExpectRejected(outcome, SemanticDispositionV1::kForbiddenAncestor);
  EXPECT_EQ(0u, outcome.audit.structural_reads);
}


TEST_F(SelectedSemanticPolicyTest, ProvenanceInvalidationAndClearRemainUnavailable) {
  for (const auto fault : {SemanticFault::kInvalidateItems, SemanticFault::kClearItems}) {
    SetBodyInnerHTML("<p id=target>Allowed</p>");
    ExpectSemanticFault(TargetText(), fault);
  }
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceRejectsUnrelatedInlineData) {
  SetBodyInnerHTML("<p id=target>Allowed</p>");
  ExpectSemanticFault(TargetText(), SemanticFault::kWrongData);
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceAmbientComparisonCannotBypassObserver) {
  SetBodyInnerHTML("<p id=target>Allowed</p>");
  ExpectSemanticFault(TargetText(), SemanticFault::kCompareDuringObserver);
}

TEST_F(SelectedSemanticPolicyTest, DirtyAXSourceAndAncestorAreNotRefreshed) {
  for (const auto fault : {SemanticFault::kDirtySourceAX, SemanticFault::kDirtyAncestorAX}) {
    SetBodyInnerHTML("<div title='Excluded title'><p id=target title='Excluded alt'>Allowed</p></div>");
    ExpectSemanticFault(TargetText(), fault);
  }
}


TEST_F(SelectedSemanticPolicyTest, RetainedStaticInlineOverflowFlagsUseOwnText) {
  SetBodyInnerHTML("<div><span id=target>Allowed text</span></div>");
  ExpectStaticInlineReadiness(InlineReadinessFault::kNone);
}

TEST_F(SelectedSemanticPolicyTest, DirtyInlineLayoutSourceTextAndBoxOverflowReject) {
  for (const auto fault : {InlineReadinessFault::kSourceTextChildOverflow,
                           InlineReadinessFault::kContainingBoxChildOverflow,
                           InlineReadinessFault::kContainingBoxSelfOverflow,
                           InlineReadinessFault::kLayout}) {
    SCOPED_TRACE(static_cast<unsigned>(fault));
    SetBodyInnerHTML("<div><span id=target>Allowed text</span></div>");
    ExpectStaticInlineReadiness(fault);
  }
}

TEST_F(SelectedSemanticPolicyTest, DirtyStaticInlinePaintStateRejectsUnchanged) {
  for (const auto fault : {InlineReadinessFault::kOwnPaint,
                           InlineReadinessFault::kDescendantPaint,
                           InlineReadinessFault::kSubtreePaint}) {
    SCOPED_TRACE(static_cast<unsigned>(fault));
    SetBodyInnerHTML("<div><span id=target>Allowed text</span></div>");
    ExpectStaticInlineReadiness(fault);
  }
}

TEST_F(SelectedSemanticPolicyTest, InlineOverflowExceptionRejectsOtherCapturePaths) {
  SetBodyInnerHTML("<div><span id=target style='position:relative'>Excluded text</span></div>");
  ExpectRejected(RunSemantic(TargetText()), SemanticDispositionV1::kNotReady);
  SetBodyInnerHTML("<div><span><button id=target aria-label='Excluded'></button></span></div>");
  ExpectRejected(RunSemantic(GetElementById("target")), SemanticDispositionV1::kNotReady);
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceSourceLength4096And4097) {
  LoadAhem();
  for (unsigned length : {4096u, 4097u}) {
    SCOPED_TRACE(length);
    SetBodyInnerHTML("<p id=target style='margin:0;width:600px;white-space:normal;overflow-wrap:anywhere;"
                    "font:1px/2px Ahem'></p>");
    StringBuilder value;
    for (unsigned i = 0; i < length; ++i) value.Append('A');
    GetElementById("target")->AppendChild(Text::Create(GetDocument(), value.ToString()));
    // Positive 1px font metrics and real wrapping keep all 4096 units
    // within the existing viewport, without subpixel font rounding to zero.
    ExpectRealProvenanceBoundary(length, 1u, true);
  }
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceRealOwnItemCount512And513) {
  for (unsigned count : {512u, 513u}) {
    SCOPED_TRACE(count);
    SetBodyInnerHTML("<p id=target style='direction:ltr;unicode-bidi:normal;"
                    "white-space:pre'> </p>");
    StringBuilder value;
    for (unsigned i = 0; i < count; ++i)
      value.Append(static_cast<UChar>((i & 1) ? 0x05D0 : 'A'));
    To<Text>(TargetText())->setData(value.ToString());
    // Alternating L/R strong code points naturally split normal text items at
    // each bidi run. The helper ASSERTS actual count/type/owner/contiguity; if
    // adopted segmentation differs, retain the failed setup and investigate.
    // Never substitute fabricated items or weaken this exact-count assertion.
    ExpectRealProvenanceBoundary(count, count, false);
  }
}

TEST_F(SelectedSemanticPolicyTest, ProvenanceInvalidAssociationRangesStayUnavailable) {
  SetBodyInnerHTML("<p id=target>Allowed</p>");
  ExpectRealProvenanceBoundary(7u, 1u, false, true);
}

class SelectedSemanticRequestTestPeer {
 public:
  static void Ready(SelectedSemanticRequestV1& request) { request.OnAXReady(); }
  static void Deadline(SelectedSemanticRequestV1& request) { request.OnDeadline(); }
  static auto TakeButtonBindings(SelectedSemanticRequestV1& request) {
    return request.TakeButtonBindings();
  }
  static bool Pending(const SelectedSemanticRequestV1& request) {
    return !request.terminal_;
  }
  static std::unique_ptr<SelectedSemanticRequestV1> CreateWithClock(
      Document& document, AXObjectCacheImpl& cache, Node& node, uint64_t epoch,
      base::TimeTicks deadline,
      scoped_refptr<base::SingleThreadTaskRunner> runner,
      SelectedSemanticRequestV1::Completion completion,
      const base::TickClock& clock,
      SemanticRequestKindV1 kind = SemanticRequestKindV1::kNode) {
    CHECK(IsMainThread());
    auto request = std::unique_ptr<SelectedSemanticRequestV1>(
        new SelectedSemanticRequestV1(document, cache, node, epoch, deadline,
                                      std::move(runner), std::move(completion),
                                      &clock, kind));
    request->Start();
    return request;
  }
};

class SelectedSemanticRequestTest : public RenderingTest {
 protected:
  SelectedSemanticRequestTest()
      : RenderingTest(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  void SetUp() override {
    RenderingTest::SetUp();
    ax_context_ = std::make_unique<AXContext>(GetDocument(), ui::kAXModeDefaultForTests);
  }
  void TearDown() override {
    ax_context_.reset();
    RenderingTest::TearDown();
  }
  void SetReadyBody(const String& html = "<p id=target>Allowed</p>") {
    SetBodyInnerHTML(html);
    cache_ = To<AXObjectCacheImpl>(GetDocument().ExistingAXObjectCache());
    ASSERT_NE(nullptr, cache_.Get());
    cache_->MarkDocumentDirty();
    cache_->UpdateAXForAllDocuments();
  }
  Node* Target() { return GetElementById("target")->firstChild(); }
  using Results = std::vector<SemanticRequestResultV1>;
  std::unique_ptr<SelectedSemanticRequestV1> Request(
      Node& target, std::shared_ptr<Results> results, uint64_t epoch = 7,
      base::TimeDelta duration = base::Seconds(1)) {
    return SelectedSemanticRequestV1::Create(
        GetDocument(), *cache_, target, epoch, base::TimeTicks::Now() + duration,
        task_environment().GetMainThreadTaskRunner(),
        blink::BindOnce([](WeakPersistent<AXObjectCacheImpl> cache,
                           std::shared_ptr<Results> results,
                           SemanticRequestResultV1 result) {
          // Client completion must never execute inside frozen capture.
          EXPECT_FALSE(cache && cache->IsFrozen());
          results->push_back(std::move(result));
        }, cache_, std::move(results)));
  }
  std::unique_ptr<SelectedSemanticRequestV1> RequestDocument(
      std::shared_ptr<Results> results, uint64_t epoch = 7,
      base::TimeDelta duration = base::Seconds(1)) {
    return SelectedSemanticRequestV1::CreateDocument(
        GetDocument(), *cache_, epoch, base::TimeTicks::Now() + duration,
        task_environment().GetMainThreadTaskRunner(),
        blink::BindOnce([](WeakPersistent<AXObjectCacheImpl> cache,
                           std::shared_ptr<Results> results,
                           SemanticRequestResultV1 result) {
          EXPECT_FALSE(cache && cache->IsFrozen());
          results->push_back(std::move(result));
        }, cache_, std::move(results)));
  }
  void DriveAX() {
    // Ambient test driver, not a request implementation preparation call.
    cache_->MarkDocumentDirty();
    cache_->UpdateAXForAllDocuments();
  }
  void Deliver() { task_environment().RunUntilIdle(); }
  void ExpectUnread(const SemanticRequestResultV1& result,
                    SemanticRequestTerminalV1 terminal) {
    EXPECT_EQ(terminal, result.terminal);
    EXPECT_TRUE(result.node.text.empty());
    EXPECT_EQ(0u, result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.structural_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
  }
  WeakPersistent<AXObjectCacheImpl> cache_;
  std::unique_ptr<AXContext> ax_context_;
};

void RecordWebCapture(std::vector<WebSelectedSemanticCaptureV1>* results,
                      WebSelectedSemanticCaptureV1 result) {
  results->push_back(std::move(result));
}
void RecordWebPress(std::vector<WebSelectedSemanticPressV1>* results,
                    WebSelectedSemanticPressV1 result);
void RecordWebVerify(std::vector<WebSelectedSemanticVerifyV1>* results,
                     WebSelectedSemanticVerifyV1 result);

TEST_F(SelectedSemanticRequestTest, WebSessionBindsOnlyLiveAdmittedButtons) {
  SetReadyBody("<p>Visible</p>"
               "<button id=first aria-label='Same'></button>"
               "<button id=second aria-label='Same'></button>"
               "<div aria-hidden=true><button aria-label='Canary'></button></div>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  EXPECT_TRUE(results.empty());
  Deliver();
  ASSERT_EQ(1u, results.size());
  const auto& result = results.front();
  ASSERT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult, result.terminal);
  ASSERT_EQ(WebSelectedSemanticDispositionV1::kAdmitted, result.disposition);
  ASSERT_EQ(3u, result.entries.size());
  EXPECT_EQ(WebSelectedSemanticRoleV1::kText, result.entries[0].role);
  EXPECT_EQ(WebSelectedSemanticRoleV1::kButton, result.entries[1].role);
  EXPECT_EQ(WebSelectedSemanticRoleV1::kButton, result.entries[2].role);
  EXPECT_EQ(0u, result.entries[0].button_slot);
  EXPECT_NE(0u, result.entries[1].button_slot);
  EXPECT_NE(result.entries[1].button_slot, result.entries[2].button_slot);
  EXPECT_TRUE(session.HasLiveButtonSlot(result.entries[1].button_slot));
  EXPECT_TRUE(session.HasLiveButtonSlot(result.entries[2].button_slot));
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

// Diagnostic only: a fixed-size status slot is a proposed action-fixture
// constraint, not part of the accepted fixture or a guarded action proof.
TEST_F(SelectedSemanticRequestTest,
       SizeContainedStatusCandidateMustPassFrozenCapture) {
  SetReadyBody("<div id=status style='width:150px;height:60px;overflow:hidden;"
               "contain:size'><p style='margin:0;font:16px/20px sans-serif'>"
               "pending</p></div><button aria-label='Press'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult,
            results[0].terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted,
            results[0].disposition);
  ASSERT_EQ(2u, results[0].entries.size());
  EXPECT_EQ("pending", results[0].entries[0].text);
  EXPECT_EQ("Press", results[0].entries[1].text);

  auto* status = GetElementById("status");
  ASSERT_NE(nullptr, status);
  auto* paragraph = status->firstElementChild();
  ASSERT_NE(nullptr, paragraph);
  auto* text = DynamicTo<Text>(paragraph->firstChild());
  ASSERT_NE(nullptr, text);
  text->setData("complete");
  GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kTest);
  session.Capture(8, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult,
            results[1].terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted,
            results[1].disposition);
  ASSERT_EQ(2u, results[1].entries.size());
  EXPECT_EQ("complete", results[1].entries[0].text);
  EXPECT_EQ("Press", results[1].entries[1].text);
}

TEST_F(SelectedSemanticRequestTest, WebSessionNewCaptureRevokesQueuedSuccess) {
  SetReadyBody("<button aria-label='First'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();  // First success is terminal, but still queued for delivery.
  session.Capture(8, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult,
            results[1].terminal);
  ASSERT_EQ(1u, results[1].entries.size());
  EXPECT_TRUE(session.HasLiveButtonSlot(results[1].entries[0].button_slot));
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionQueuedCancellationPreservesCaptureAudit) {
  SetReadyBody("<button aria-label='Visible'></button>"
               "<button aria-label='Zero canary' style='appearance:none;"
               "width:0;height:20px;padding:0;border:0'></button>"
               "<button aria-label='Offscreen canary' style='position:relative;"
               "left:900px;width:40px;height:20px'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  ASSERT_TRUE(results.empty());
  session.Cancel();
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kCancelled, results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_EQ(0u, results[0].reserved_output_bytes);
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
  EXPECT_EQ(1u, results[0].audit.text_reads);
  EXPECT_EQ(0u, results[0].audit.forbidden_reads);
  EXPECT_EQ(7u, results[0].budget.utf8_bytes);
  EXPECT_EQ(1u, results[0].audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, results[0].audit.original_wholly_offscreen_exclusions);
}

TEST_F(SelectedSemanticRequestTest, WebSessionCancelRevokesQueuedAndDeliveredSlots) {
  SetReadyBody("<button aria-label='First'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  session.Cancel();
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kCancelled, results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  session.Capture(8, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(2u, results.size());
  ASSERT_EQ(1u, results[1].entries.size());
  const auto slot = results[1].entries[0].button_slot;
  ASSERT_TRUE(session.HasLiveButtonSlot(slot));
  session.Cancel();
  EXPECT_FALSE(session.HasLiveButtonSlot(slot));
}

TEST_F(SelectedSemanticRequestTest, WebSessionExpiryHasNoEntriesOrSlots) {
  SetReadyBody("<button aria-label='First'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Milliseconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  task_environment().FastForwardBy(base::Milliseconds(1));
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kDeadlineExceeded,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
  DriveAX();
  Deliver();
  EXPECT_EQ(1u, results.size());
}

TEST_F(SelectedSemanticRequestTest, WebSessionRemovedSourceCannotReuseSlot) {
  SetReadyBody("<button id=first aria-label='First'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  ASSERT_EQ(1u, results[0].entries.size());
  const auto slot = results[0].entries[0].button_slot;
  ASSERT_TRUE(session.HasLiveButtonSlot(slot));
  WeakPersistent<Node> removed(GetElementById("first"));
  GetElementById("first")->remove();
  DriveAX();
  ThreadState::Current()->CollectAllGarbageForTesting();
  EXPECT_EQ(nullptr, removed.Get());
  EXPECT_FALSE(session.HasLiveButtonSlot(slot));
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionNeverTransmitsOwnZeroOrOffscreenCanaries) {
  SetReadyBody(
      "<p>Allowed positive</p>"
      "<button aria-label='Zero canary' style='appearance:none;width:0;"
      "height:20px;padding:0;border:0;overflow:visible'></button>"
      "<button aria-label='Offscreen canary' style='position:relative;"
      "left:900px;width:40px;height:20px'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  const auto& result = results.front();
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted, result.disposition);
  ASSERT_EQ(1u, result.entries.size());
  EXPECT_EQ(WebSelectedSemanticRoleV1::kText, result.entries[0].role);
  EXPECT_EQ("Allowed positive", result.entries[0].text);
  EXPECT_EQ(0u, result.entries[0].button_slot);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionReportsSourceBackedExclusionCounts) {
  SetReadyBody(
      "<style>html,body{overflow:hidden}</style>"
      "<p>Allowed positive</p>"
      "<div aria-hidden=true><button aria-label='AX-pruned canary'></button></div>"
      "<div contenteditable=true>Editable subtree canary</div>"
      "<button aria-label='Zero canary' style='appearance:none;width:0;"
      "height:20px;padding:0;border:0;overflow:visible'></button>"
      "<button aria-label='Offscreen canary' style='position:relative;"
      "left:900px;width:40px;height:20px'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  const auto& result = results.front();
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted, result.disposition);
  ASSERT_EQ(1u, result.entries.size());
  EXPECT_EQ(WebSelectedSemanticRoleV1::kText, result.entries[0].role);
  EXPECT_EQ("Allowed positive", result.entries[0].text);
  EXPECT_EQ(0u, result.entries[0].button_slot);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  EXPECT_EQ(1u, result.audit.forbidden_structure_prunes);
  EXPECT_EQ(1u, result.audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, result.audit.original_wholly_offscreen_exclusions);
  EXPECT_LE(result.budget.nodes, 256u);
}

// Reproduce the selected page from the CEF two-view consumer with an actual
// HTML document parse. A paragraph control must not conceal button loss.
class SelectedSemanticLiveFixtureParityTest : public SimTest {
 protected:
  void TearDown() override {
    ax_context_.reset();
    SimTest::TearDown();
  }

  void LoadSelectedPage(bool paragraph_before_buttons,
                        bool force_ax_priming = true,
                        bool cef_ax_mode = false,
                        float device_scale_factor = 1.0f,
                        bool body_css_zoom = false) {
    constexpr char kURL[] = "https://lunar-policy.test/selected-parity";
    ResizeView(gfx::Size(900, 618));
    SimRequest resource(kURL, "text/html");
    LoadURL(kURL);
    GetPage().SetFocused(true);
    StringBuilder html;
    html.Append("<html><style>html,body{overflow:hidden}</style>"
                "<body style='margin:0");
    if (body_css_zoom)
      html.Append(";zoom:2");
    html.Append("'>");
    if (paragraph_before_buttons)
      html.Append("<p>known-positive-paragraph</p>");
    html.Append(
        "<button aria-label='selected-lunar-alpha' "
        "style='width:200px;height:32px'>selected-lunar-alpha"
        "</button>"
        "<div contenteditable=true>editable-lunar-canary</div>"
        "<button aria-label='zero-lunar-canary' style='appearance:none;"
        "width:0;height:20px;padding:0;border:0;overflow:visible'>"
        "</button>"
        "<button aria-label='offscreen-lunar-canary' "
        "style='position:relative;left:900px;width:40px;height:20px'>"
        "</button></body></html>");
    resource.Complete(html.ToString());
    WebView().SetZoomFactorForDeviceScaleFactor(device_scale_factor, 1.0f);
    ax_context_ = std::make_unique<AXContext>(
        GetDocument(),
        cef_ax_mode ? ui::kAXModeComplete : ui::kAXModeDefaultForTests);
    cache_ = To<AXObjectCacheImpl>(GetDocument().ExistingAXObjectCache());
    ASSERT_NE(nullptr, cache_.Get());
    if (force_ax_priming) {
      cache_->MarkDocumentDirty();
      cache_->UpdateAXForAllDocuments();
    }
  }

  void Capture(std::vector<WebSelectedSemanticCaptureV1>& results,
               bool force_ax_priming = true) {
    WebSelectedSemanticSession session(
        WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
    session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                    base::BindOnce(&RecordWebCapture, &results));
    if (force_ax_priming) {
      cache_->MarkDocumentDirty();
      cache_->UpdateAXForAllDocuments();
    } else {
      // SimTest needs an explicit compositor frame to service the requested
      // lifecycle callback; this is not a manual AX dirty/update call.
      Compositor().BeginFrame();
    }
    task_environment().RunUntilIdle();
  }

  std::unique_ptr<AXContext> ax_context_;
  WeakPersistent<AXObjectCacheImpl> cache_;
};

void ExpectSelectedLiveFixtureResult(const WebSelectedSemanticCaptureV1& result,
                                     bool paragraph_before_buttons) {
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted, result.disposition);
  unsigned selected_buttons = 0;
  unsigned control_paragraphs = 0;
  for (const auto& entry : result.entries) {
    if (entry.role == WebSelectedSemanticRoleV1::kButton &&
        entry.text == "selected-lunar-alpha") {
      ++selected_buttons;
      EXPECT_NE(0u, entry.button_slot);
    }
    if (entry.role == WebSelectedSemanticRoleV1::kText &&
        entry.text == "known-positive-paragraph") {
      ++control_paragraphs;
    }
    EXPECT_NE("zero-lunar-canary", entry.text);
    EXPECT_NE("offscreen-lunar-canary", entry.text);
    EXPECT_NE("editable-lunar-canary", entry.text);
  }
  EXPECT_EQ(1u, selected_buttons);
  EXPECT_EQ(paragraph_before_buttons ? 1u : 0u, control_paragraphs);
  EXPECT_EQ(1u, result.audit.forbidden_structure_prunes);
  EXPECT_EQ(1u, result.audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, result.audit.original_wholly_offscreen_exclusions);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticLiveFixtureParityTest, ExactCefSelectedPage) {
  LoadSelectedPage(false);
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results);
  ASSERT_EQ(1u, results.size());
  ExpectSelectedLiveFixtureResult(results.front(), false);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       ExactCefSelectedPageWithParagraphControl) {
  LoadSelectedPage(true);
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results);
  ASSERT_EQ(1u, results.size());
  ExpectSelectedLiveFixtureResult(results.front(), true);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       ExactCefSelectedPageWithoutForcedAXPriming) {
  LoadSelectedPage(false, false);
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  ExpectSelectedLiveFixtureResult(results.front(), false);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       ExactCefSelectedPageCefAXModeWithoutForcedPriming) {
  LoadSelectedPage(false, false, true);
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  ExpectSelectedLiveFixtureResult(results.front(), false);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       ExactCefSelectedPageAtRetinaDeviceScale) {
  LoadSelectedPage(false, false, true, 2.0f);
  ASSERT_EQ(2.0f, GetDocument().GetFrame()->LayoutZoomFactor());
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  ExpectSelectedLiveFixtureResult(results.front(), false);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       ParagraphAndButtonsAtFractionalDeviceScale) {
  LoadSelectedPage(true, false, true, 1.25f);
  ASSERT_EQ(1.25f, GetDocument().GetFrame()->LayoutZoomFactor());
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  ExpectSelectedLiveFixtureResult(results.front(), true);
  EXPECT_EQ(2u, results.front().audit.text_reads);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       RetinaDeviceScaleDoesNotAdmitUserZoom) {
  LoadSelectedPage(false, false, true, 2.0f);
  WebView().MainFrameWidget()->SetZoomLevel(1.0);
  ASSERT_NE(2.0f, GetDocument().GetFrame()->LayoutZoomFactor());
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  EXPECT_TRUE(results.front().entries.empty());
  EXPECT_EQ(0u, results.front().audit.text_reads);
  EXPECT_EQ(0u, results.front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       RetinaDeviceScaleRejectsUserZoomEvenWhenLayoutFloatAliases) {
  LoadSelectedPage(false, false, true, 2.0f);
  WebView().MainFrameWidget()->SetZoomLevel(1e-8);
  ASSERT_EQ(2.0f, GetDocument().GetFrame()->LayoutZoomFactor());
  ASSERT_NE(1.0, GetDocument().GetPage()->GetChromeClient().UserZoomFactor(
                     GetDocument().GetFrame()));
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  EXPECT_TRUE(results.front().entries.empty());
  EXPECT_EQ(0u, results.front().audit.text_reads);
  EXPECT_EQ(0u, results.front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       QueuedRetinaCaptureRejectsAliasedUserZoomAndPreservesAudit) {
  LoadSelectedPage(false, true, false, 2.0f);
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  cache_->MarkDocumentDirty();
  cache_->UpdateAXForAllDocuments();
  ASSERT_TRUE(results.empty());
  const uint64_t tree_version = GetDocument().DomTreeVersion();
  const uint64_t style_version = GetDocument().StyleVersion();
  const uint64_t layout_generation =
      GetDocument().View()->LayoutGenerationForSelectedSemantic();
  WebView().MainFrameWidget()->SetZoomLevel(1e-8);
  ASSERT_EQ(2.0f, GetDocument().GetFrame()->LayoutZoomFactor());
  ASSERT_NE(1.0, GetDocument().GetPage()->GetChromeClient().UserZoomFactor(
                     GetDocument().GetFrame()));
  ASSERT_EQ(tree_version, GetDocument().DomTreeVersion());
  ASSERT_EQ(style_version, GetDocument().StyleVersion());
  ASSERT_EQ(layout_generation,
            GetDocument().View()->LayoutGenerationForSelectedSemantic());
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results.front().terminal);
  EXPECT_TRUE(results.front().entries.empty());
  EXPECT_EQ(0u, results.front().reserved_output_bytes);
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
  EXPECT_EQ(1u, results.front().audit.text_reads);
  EXPECT_EQ(0u, results.front().audit.forbidden_reads);
  EXPECT_EQ(1u, results.front().audit.forbidden_structure_prunes);
  EXPECT_EQ(1u, results.front().audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, results.front().audit.original_wholly_offscreen_exclusions);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       DeliveredRetinaSlotRejectsAliasedUserZoom) {
  LoadSelectedPage(false, true, false, 2.0f);
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  cache_->MarkDocumentDirty();
  cache_->UpdateAXForAllDocuments();
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, results.size());
  ASSERT_EQ(1u, results.front().entries.size());
  const uint64_t slot = results.front().entries.front().button_slot;
  ASSERT_NE(0u, slot);
  ASSERT_TRUE(session.HasLiveButtonSlot(slot));
  const uint64_t tree_version = GetDocument().DomTreeVersion();
  const uint64_t style_version = GetDocument().StyleVersion();
  const uint64_t layout_generation =
      GetDocument().View()->LayoutGenerationForSelectedSemantic();
  WebView().MainFrameWidget()->SetZoomLevel(1e-8);
  ASSERT_EQ(2.0f, GetDocument().GetFrame()->LayoutZoomFactor());
  ASSERT_NE(1.0, GetDocument().GetPage()->GetChromeClient().UserZoomFactor(
                     GetDocument().GetFrame()));
  ASSERT_EQ(tree_version, GetDocument().DomTreeVersion());
  ASSERT_EQ(style_version, GetDocument().StyleVersion());
  ASSERT_EQ(layout_generation,
            GetDocument().View()->LayoutGenerationForSelectedSemantic());
  EXPECT_FALSE(session.HasLiveButtonSlot(slot));
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       QueuedRetinaCaptureRejectsDeviceScaleChange) {
  LoadSelectedPage(false, true, false, 2.0f);
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  cache_->MarkDocumentDirty();
  cache_->UpdateAXForAllDocuments();
  ASSERT_TRUE(results.empty());
  WebView().SetZoomFactorForDeviceScaleFactor(1.25f, 1.0f);
  ASSERT_EQ(1.25f, GetDocument().GetFrame()->LayoutZoomFactor());
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results.front().terminal);
  EXPECT_TRUE(results.front().entries.empty());
  EXPECT_EQ(0u, results.front().reserved_output_bytes);
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
  EXPECT_EQ(1u, results.front().audit.text_reads);
  EXPECT_EQ(0u, results.front().audit.forbidden_reads);
  EXPECT_EQ(1u, results.front().audit.forbidden_structure_prunes);
  EXPECT_EQ(1u, results.front().audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, results.front().audit.original_wholly_offscreen_exclusions);
}

TEST_F(SelectedSemanticLiveFixtureParityTest,
       RetinaDeviceScaleDoesNotAdmitCssZoom) {
  LoadSelectedPage(false, false, true, 2.0f, true);
  ASSERT_EQ(2.0f, GetDocument().GetFrame()->LayoutZoomFactor());
  std::vector<WebSelectedSemanticCaptureV1> results;
  Capture(results, false);
  ASSERT_EQ(1u, results.size());
  EXPECT_TRUE(results.front().entries.empty());
  EXPECT_EQ(0u, results.front().audit.text_reads);
  EXPECT_EQ(0u, results.front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionCountsWholeClipNotPartialClipOrUnsupported) {
  LoadAhem();
  SetReadyBody(
      "<p>Allowed positive</p>"
      "<div style='overflow:hidden;width:100px;height:40px'>"
      "<button aria-label='Clipped canary' style='position:relative;"
      "left:120px;width:40px;height:20px'></button></div>"
      "<div style='width:20px;overflow:hidden'>"
      "<p style='font:10px/12px Ahem;width:200px;white-space:pre'>"
      "Partially clipped text</p></div>"
      "<button aria-label='Unsupported canary' style='transform:rotate(20deg)'>"
      "</button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  const auto& result = results.front();
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted, result.disposition);
  ASSERT_EQ(1u, result.entries.size());
  EXPECT_EQ("Allowed positive", result.entries[0].text);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  EXPECT_EQ(0u, result.audit.forbidden_structure_prunes);
  EXPECT_EQ(0u, result.audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, result.audit.original_wholly_offscreen_exclusions);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionExclusionCountCannotOutrunTraversalBound) {
  StringBuilder html;
  html.Append("<p>Allowed positive</p>");
  // Group children so the >256 AX descendants are traversed before the
  // per-parent child-count guard rejects the document.
  for (unsigned group = 0; group < 9; ++group) {
    html.Append("<div role=group>");
    for (unsigned i = 0; i < 29; ++i) {
      html.Append("<button aria-label='Zero canary' style='appearance:none;"
                  "width:0;height:20px;padding:0;border:0'></button>");
    }
    html.Append("</div>");
  }
  SetReadyBody(html.ToString());
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult,
            results[0].terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kLimitExceeded,
            results[0].disposition);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_EQ(0u, results[0].reserved_output_bytes);
  EXPECT_EQ(0u, results[0].audit.forbidden_reads);
  EXPECT_GT(results[0].audit.original_zero_size_exclusions, 0u);
  EXPECT_LE(results[0].audit.forbidden_structure_prunes +
                results[0].audit.original_zero_size_exclusions +
                results[0].audit.original_wholly_offscreen_exclusions,
            256u);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionDoesNotCountOutOfDomainOffscreenButton) {
  SetReadyBody(
      "<style>html,body{overflow:hidden}</style>"
      "<p>Allowed positive</p>"
      "<div style='position:relative;left:-32718px'>"
      "<button aria-label='Unproven offscreen canary' style='position:relative;"
      "left:40000px;width:40px;height:20px'></button></div>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  const auto& result = results.front();
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted, result.disposition);
  ASSERT_EQ(1u, result.entries.size());
  EXPECT_EQ("Allowed positive", result.entries[0].text);
  EXPECT_EQ(0u, result.entries[0].button_slot);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  EXPECT_EQ(0u, result.audit.original_wholly_offscreen_exclusions);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionMixedEmptyTextFragmentIsExcludedWithoutZeroSizeClaim) {
  SetReadyBody("<p>Allowed positive</p>"
               "<p id=mixed style='white-space:pre-wrap'>\nB</p>");
  auto* text = To<Text>(GetElementById("mixed")->firstChild());
  auto* layout_text = DynamicTo<LayoutText>(text->GetLayoutObject());
  ASSERT_NE(nullptr, layout_text);
  const auto* container = layout_text->FragmentItemsContainer();
  ASSERT_NE(nullptr, container);
  const auto* fragment = container->GetPhysicalFragment(0);
  ASSERT_NE(nullptr, fragment);
  const auto* items = fragment->Items();
  ASSERT_NE(nullptr, items);
  const auto span = items->Items();
  const auto first = layout_text->FirstInlineFragmentItemIndex();
  ASSERT_GT(first, 0u);
  ASSERT_TRUE(span[first - 1].RectInContainerFragment().IsEmpty());
  unsigned index = first - 1;
  unsigned positive = 0;
  unsigned empty = 0;
  for (;;) {
    ASSERT_LT(index, span.size());
    const auto& item = span[index];
    ASSERT_EQ(layout_text, item.GetLayoutObject());
    ASSERT_EQ(FragmentItem::kText, item.Type());
    if (item.RectInContainerFragment().IsEmpty())
      ++empty;
    else
      ++positive;
    const auto delta = item.DeltaToNextForSameLayoutObject();
    if (!delta)
      break;
    index += delta;
  }
  ASSERT_GT(positive, 0u);
  ASSERT_GT(empty, 0u);

  auto node_results = std::make_shared<Results>();
  auto node_request = Request(*text, node_results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, node_results->size());
  // The flow-control newline fails exact text identity before the geometry
  // classifier; mixed fragments therefore cannot support a zero-size claim.
  EXPECT_EQ(SemanticDispositionV1::kUnsupportedText,
            node_results->front().node.disposition);
  EXPECT_EQ(0u, node_results->front().audit.text_reads);

  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(8, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult,
            results[0].terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kAdmitted,
            results[0].disposition);
  ASSERT_EQ(1u, results[0].entries.size());
  EXPECT_EQ("Allowed positive", results[0].entries[0].text);
  EXPECT_EQ(0u, results[0].entries[0].button_slot);
  EXPECT_EQ(1u, results[0].audit.text_reads);
  EXPECT_EQ(0u, results[0].audit.forbidden_reads);
  EXPECT_EQ(0u, results[0].audit.original_zero_size_exclusions);
}

// RED prototype for the service-only guard. These tests intentionally call
// Chromium's current native AX default path to pin its script stages before
// Task4b changes that path; they are not a public action interface.
class SelectedSemanticActionPrototypeTest : public SimTest {
 protected:
  void TearDown() override {
    ax_context_.reset();
    SimTest::TearDown();
  }

  void LoadPrototype(const char* button_type, const char* handlers) {
    constexpr char kURL[] = "https://lunar-policy.test/action-prototype";
    ResizeView(gfx::Size(800, 600));
    SimRequest resource(kURL, "text/html");
    LoadURL(kURL);
    GetPage().SetFocused(true);
    StringBuilder html;
    html.Append("<!doctype html><html><body><form id=form></form>"
                "<button id=target form=form type='");
    html.Append(button_type);
    html.Append("' aria-label='Press'></button>"
                "<p id=action-status>pending</p><p id=clicks>0</p>"
                "<p id=pointerdowns>0</p><p id=submits>0</p><script>"
                "const button=document.getElementById('target');"
                "const bump=id=>{const text=document.getElementById(id).firstChild;"
                "text.data=String(Number(text.data)+1)};"
                "document.getElementById('form').addEventListener('submit',"
                "event=>{event.preventDefault();bump('submits')});");
    html.Append(handlers);
    html.Append("</script></body></html>");
    resource.Complete(html.ToString());
    ax_context_ =
        std::make_unique<AXContext>(GetDocument(), ui::kAXModeDefaultForTests);
    auto* cache =
        To<AXObjectCacheImpl>(GetDocument().ExistingAXObjectCache());
    ASSERT_NE(nullptr, cache);
    cache->MarkDocumentDirty();
    cache->UpdateAXForAllDocuments();
    ASSERT_NE(nullptr, Button());
  }

  Element* ById(const char* id) {
    return GetDocument().getElementById(AtomicString(id));
  }
  Element* Button() { return ById("target"); }
  String TextAt(const char* id) { return ById(id)->firstChild()->nodeValue(); }
  bool PressNativeDefault() {
    auto* cache =
        To<AXObjectCacheImpl>(GetDocument().ExistingAXObjectCache());
    Element* button = Button();
    if (!cache || !button || !button->GetLayoutObject())
      return false;
    AXObject* object = cache->Get(button);
    if (!object)
      return false;
    Node* const parent = button->parentNode();
    const DocumentToken document_token = GetDocument().Token();
    const gfx::Rect bounds =
        button->GetLayoutObject()->AbsoluteBoundingBoxRect();
    auto guard = base::BindRepeating(
        [](WeakPersistent<Element> button, WeakPersistent<Node> parent,
           WeakPersistent<Document> document, DocumentToken document_token,
           gfx::Rect bounds) {
          Element* current = button.Get();
          return current && document &&
                 document->Token() == document_token &&
                 current->isConnected() &&
                 &current->GetDocument() == document.Get() &&
                 current->parentNode() == parent.Get() &&
                 current->FastGetAttribute(html_names::kAriaLabelAttr) ==
                     "Press" &&
                 current->GetLayoutObject() &&
                 current->GetLayoutObject()->AbsoluteBoundingBoxRect() ==
                     bounds;
        },
        WrapWeakPersistent(button), WrapWeakPersistent(parent),
        WrapWeakPersistent(&GetDocument()), document_token, bounds);
    return object->PerformSelectedSemanticButtonPress(guard) !=
           SelectedSemanticPressDispatchResult::kNotStarted;
  }
  void RestoreTargetAndStatus() {
    Button()->setAttribute(html_names::kAriaLabelAttr, AtomicString("Press"));
    To<Text>(ById("action-status")->firstChild())->setData("pending");
  }
  void DriveAXAndDeliver() {
    auto* cache =
        To<AXObjectCacheImpl>(GetDocument().ExistingAXObjectCache());
    ASSERT_NE(nullptr, cache);
    cache->MarkDocumentDirty();
    cache->UpdateAXForAllDocuments();
    task_environment().RunUntilIdle();
  }
  uint64_t CaptureActionSession(
      WebSelectedSemanticSession& session,
      std::vector<WebSelectedSemanticCaptureV1>& results,
      uint64_t epoch = 7) {
    session.Capture(epoch, base::TimeTicks::Now() + base::Seconds(1),
                    base::BindOnce(&RecordWebCapture, &results));
    DriveAXAndDeliver();
    if (results.empty())
      return 0;
    for (const auto& entry : results.back().entries) {
      if (entry.role == WebSelectedSemanticRoleV1::kButton)
        return entry.button_slot;
    }
    return 0;
  }

  std::unique_ptr<AXContext> ax_context_;
};

void RecordWebPress(std::vector<WebSelectedSemanticPressV1>* results,
                    WebSelectedSemanticPressV1 result) {
  results->push_back(std::move(result));
}

void RecordWebVerify(std::vector<WebSelectedSemanticVerifyV1>* results,
                     WebSelectedSemanticVerifyV1 result) {
  results->push_back(std::move(result));
}

TEST_F(SelectedSemanticActionPrototypeTest, NativeButtonPositiveChangesStatus) {
  LoadPrototype("button", "button.addEventListener('click',()=>{bump('clicks');"
                          "document.getElementById('action-status').firstChild.data='complete'});");
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_EQ("complete", TextAt("action-status"));
  EXPECT_FALSE(Button()->IsActive());
}

TEST_F(SelectedSemanticActionPrototypeTest, NativeButtonNoopLeavesStatusPending) {
  LoadPrototype("button", "button.addEventListener('click',()=>bump('clicks'));");
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_EQ("pending", TextAt("action-status"));
  EXPECT_FALSE(Button()->IsActive());
}

TEST_F(SelectedSemanticActionPrototypeTest,
       GuardedClickSharesRecursiveDispatchSetAndCleansUp) {
  LoadPrototype("button", "button.addEventListener('click',()=>{"
                          "bump('clicks');button.click()});");
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_FALSE(Button()->IsActive());

  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("2", TextAt("clicks"));
  EXPECT_FALSE(Button()->IsActive());
}

TEST_F(SelectedSemanticActionPrototypeTest,
       FocusMutationMustStopClickAndPermitLegitimateRepeat) {
  LoadPrototype("button", "button.addEventListener('focus',()=>{"
                          "button.setAttribute('aria-label','Changed')},{once:true});"
                          "button.addEventListener('click',()=>{bump('clicks');"
                          "document.getElementById('action-status').firstChild.data='complete'});");
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("Changed", Button()->FastGetAttribute(html_names::kAriaLabelAttr));
  EXPECT_EQ("0", TextAt("clicks"));
  EXPECT_EQ("pending", TextAt("action-status"));
  EXPECT_FALSE(Button()->IsActive());
  RestoreTargetAndStatus();
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_EQ("complete", TextAt("action-status"));
  EXPECT_FALSE(Button()->IsActive());
}

TEST_F(SelectedSemanticActionPrototypeTest,
       PointerDownMutationMustStopClickAndPermitLegitimateRepeat) {
  LoadPrototype("button", "button.addEventListener('pointerdown',()=>{"
                          "bump('pointerdowns');button.setAttribute('aria-label',"
                          "'Changed')},{once:true});"
                          "button.addEventListener('click',()=>{bump('clicks');"
                          "document.getElementById('action-status').firstChild.data='complete'});");
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("pointerdowns"));
  EXPECT_EQ("Changed", Button()->FastGetAttribute(html_names::kAriaLabelAttr));
  EXPECT_EQ("0", TextAt("clicks"));
  EXPECT_EQ("pending", TextAt("action-status"));
  EXPECT_FALSE(Button()->IsActive());
  RestoreTargetAndStatus();
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_EQ("complete", TextAt("action-status"));
  EXPECT_FALSE(Button()->IsActive());
}

TEST_F(SelectedSemanticActionPrototypeTest,
       ClickMutationMustStopSubmitDefaultAndPermitLegitimateRepeat) {
  LoadPrototype("submit", "button.addEventListener('click',()=>bump('clicks'));"
                          "button.addEventListener('click',()=>{"
                          "button.setAttribute('aria-label','Changed')},"
                          "{once:true});");
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_EQ("Changed", Button()->FastGetAttribute(html_names::kAriaLabelAttr));
  EXPECT_EQ("0", TextAt("submits"));
  EXPECT_FALSE(Button()->IsActive());
  RestoreTargetAndStatus();
  EXPECT_TRUE(PressNativeDefault());
  EXPECT_EQ("2", TextAt("clicks"));
  EXPECT_EQ("1", TextAt("submits"));
  EXPECT_FALSE(Button()->IsActive());
}

TEST_F(SelectedSemanticActionPrototypeTest,
       WebSessionPressIsOneUseAndVerifiesExactBoundText) {
  LoadPrototype("button", "button.addEventListener('click',()=>{bump('clicks');"
                          "document.getElementById('action-status').firstChild.data='complete'});");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> captures;
  const uint64_t slot = CaptureActionSession(session, captures);
  ASSERT_NE(0u, slot);

  std::vector<WebSelectedSemanticPressV1> presses;
  session.Press(slot, 7, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  DriveAXAndDeliver();
  ASSERT_EQ(1u, presses.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kEffectUncertain,
            presses[0].status);
  EXPECT_EQ(WebSelectedSemanticActuationStateV1::kStarted,
            presses[0].actuation);
  EXPECT_EQ("1", TextAt("clicks"));
  EXPECT_FALSE(session.HasLiveButtonSlot(slot));

  session.Press(slot, 8, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  task_environment().RunUntilIdle();
  ASSERT_EQ(2u, presses.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kStaleDocument,
            presses[1].status);
  EXPECT_EQ(WebSelectedSemanticActuationStateV1::kNotStarted,
            presses[1].actuation);
  EXPECT_EQ("1", TextAt("clicks"));

  std::vector<WebSelectedSemanticVerifyV1> verifies;
  session.Verify(9, base::TimeTicks::Now() + base::Seconds(1),
                 base::BindOnce(&RecordWebVerify, &verifies));
  DriveAXAndDeliver();
  ASSERT_EQ(1u, verifies.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kOk, verifies[0].status);
  EXPECT_EQ("complete", verifies[0].value);
}

TEST_F(SelectedSemanticActionPrototypeTest,
       WebSessionVerifyRejectsReplacementStatusText) {
  LoadPrototype("button", "button.addEventListener('click',()=>{"
                          "document.getElementById('action-status').firstChild.data='complete'});");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> captures;
  const uint64_t slot = CaptureActionSession(session, captures);
  ASSERT_NE(0u, slot);
  std::vector<WebSelectedSemanticPressV1> presses;
  session.Press(slot, 7, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  DriveAXAndDeliver();
  ASSERT_EQ(1u, presses.size());

  Element* status = ById("action-status");
  status->firstChild()->remove();
  status->AppendChild(Text::Create(GetDocument(), "complete"));
  GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kTest);
  std::vector<WebSelectedSemanticVerifyV1> verifies;
  session.Verify(8, base::TimeTicks::Now() + base::Seconds(1),
                 base::BindOnce(&RecordWebVerify, &verifies));
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, verifies.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kStaleDocument,
            verifies[0].status);
  EXPECT_TRUE(verifies[0].value.IsEmpty());
}

TEST_F(SelectedSemanticActionPrototypeTest,
       WebSessionBindsFixedStatusInsteadOfSearchingPendingText) {
  LoadPrototype(
      "button",
      "const unrelated=document.createElement('p');"
      "unrelated.textContent='pending';document.body.appendChild(unrelated);"
      "button.addEventListener('click',()=>{"
      "document.getElementById('action-status').firstChild.data='complete'});");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> captures;
  const uint64_t slot = CaptureActionSession(session, captures);
  ASSERT_NE(0u, slot);

  std::vector<WebSelectedSemanticPressV1> presses;
  session.Press(slot, 7, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  DriveAXAndDeliver();
  ASSERT_EQ(1u, presses.size());

  std::vector<WebSelectedSemanticVerifyV1> verifies;
  session.Verify(8, base::TimeTicks::Now() + base::Seconds(1),
                 base::BindOnce(&RecordWebVerify, &verifies));
  DriveAXAndDeliver();
  ASSERT_EQ(1u, verifies.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kOk, verifies[0].status);
  EXPECT_EQ("complete", verifies[0].value);
}

TEST_F(SelectedSemanticActionPrototypeTest,
       WebSessionQueuedCancelProvesNotStarted) {
  LoadPrototype("button", "button.addEventListener('click',()=>bump('clicks'));");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> captures;
  const uint64_t slot = CaptureActionSession(session, captures);
  ASSERT_NE(0u, slot);
  std::vector<WebSelectedSemanticPressV1> presses;
  session.Press(slot, 7, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  session.Cancel();
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, presses.size());
  EXPECT_EQ(
      WebSelectedSemanticOperationStatusV1::kCancelledBeforeActuation,
      presses[0].status);
  EXPECT_EQ(WebSelectedSemanticActuationStateV1::kNotStarted,
            presses[0].actuation);
  EXPECT_EQ("0", TextAt("clicks"));
}

// Temporary Task4b diagnostic. Observe only; a production action guard cannot
// use a later lifecycle update as proof during synchronous event dispatch.
struct SelectedSemanticGeometryProbeSnapshot {
  bool saw = false;
  bool target_connected = false;
  bool target_parent_same = false;
  bool label_same = false;
  bool target_needs_layout = false;
  bool form_needs_layout = false;
  bool body_needs_layout = false;
  bool root_needs_layout = false;
  bool view_needs_layout = false;
  bool cache_dirty = false;
  bool body_child_needs_style_recalc = false;
  gfx::Rect immediate_button;
};

class SelectedSemanticGeometryProbe final : public NativeEventListener {
 public:
  SelectedSemanticGeometryProbe(Element& button,
                                Element& form,
                                SelectedSemanticGeometryProbeSnapshot& snapshot)
      : button_(&button),
        form_(&form),
        parent_(button.parentElement()),
        snapshot_(&snapshot) {}

  void Invoke(ExecutionContext*, Event*) override {
    Document& document = button_->GetDocument();
    auto* cache =
        To<AXObjectCacheImpl>(document.ExistingAXObjectCache());
    snapshot_->saw = true;
    snapshot_->target_connected = button_->isConnected();
    snapshot_->target_parent_same = button_->parentElement() == parent_;
    snapshot_->label_same =
        button_->FastGetAttribute(html_names::kAriaLabelAttr) == "Press";
    snapshot_->target_needs_layout =
        button_->GetLayoutObject() && button_->GetLayoutObject()->NeedsLayout();
    snapshot_->form_needs_layout =
        form_->GetLayoutObject() && form_->GetLayoutObject()->NeedsLayout();
    snapshot_->body_needs_layout =
        document.body()->GetLayoutObject() &&
        document.body()->GetLayoutObject()->NeedsLayout();
    snapshot_->root_needs_layout = document.GetLayoutView() &&
                                   document.GetLayoutView()->NeedsLayout();
    snapshot_->view_needs_layout =
        document.View() && document.View()->NeedsLayout();
    snapshot_->cache_dirty = cache && cache->IsDirty();
    snapshot_->body_child_needs_style_recalc =
        document.body()->ChildNeedsStyleRecalc();
    if (button_->GetLayoutObject()) {
      snapshot_->immediate_button =
          button_->GetLayoutObject()->AbsoluteBoundingBoxRect();
    }
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(button_);
    visitor->Trace(form_);
    visitor->Trace(parent_);
    NativeEventListener::Trace(visitor);
  }

 private:
  Member<Element> button_;
  Member<Element> form_;
  Member<Element> parent_;
  SelectedSemanticGeometryProbeSnapshot* snapshot_;
};

class SelectedSemanticActionGeometryProbeTest
    : public SelectedSemanticActionPrototypeTest {
 protected:
  void RunProbe(bool status_before, bool fixed_slot, bool multiline) {
    StringBuilder handlers;
    handlers.Append("const status=document.getElementById('action-status');"
                    "status.style.cssText='display:block;margin:0;width:150px;"
                    "font-size:16px;line-height:20px;white-space:pre-line;");
    if (fixed_slot) {
      handlers.Append("height:60px;overflow:hidden;contain:size;");
    }
    handlers.Append("';");
    if (status_before) {
      handlers.Append("document.body.insertBefore(status,"
                      "document.getElementById('form'));");
    }
    handlers.Append("button.addEventListener('click',()=>{"
                    "status.firstChild.data='");
    handlers.Append(multiline ? "complete\\ncomplete" : "complete");
    handlers.Append("'});");
    LoadPrototype("button", handlers.ToString().Utf8().c_str());

    Element* status = ById("action-status");
    Element* form = ById("form");
    ASSERT_NE(nullptr, status);
    ASSERT_NE(nullptr, form);
    ASSERT_NE(nullptr, Button()->GetLayoutObject());
    const gfx::Rect before =
        Button()->GetLayoutObject()->AbsoluteBoundingBoxRect();
    SelectedSemanticGeometryProbeSnapshot snapshot;
    auto* probe = MakeGarbageCollected<SelectedSemanticGeometryProbe>(
        *Button(), *form, snapshot);
    Button()->addEventListener(event_type_names::kClick, probe, false);
    EXPECT_TRUE(PressNativeDefault());
    Button()->removeEventListener(event_type_names::kClick, probe, false);
    ASSERT_TRUE(snapshot.saw);
    EXPECT_TRUE(snapshot.target_connected);
    EXPECT_TRUE(snapshot.target_parent_same);
    EXPECT_TRUE(snapshot.label_same);
    EXPECT_EQ(before, snapshot.immediate_button);
    EXPECT_FALSE(snapshot.target_needs_layout);
    EXPECT_TRUE(snapshot.view_needs_layout);
    EXPECT_TRUE(snapshot.cache_dirty);
    GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kTest);
    ASSERT_NE(nullptr, Button()->GetLayoutObject());
    const gfx::Rect after =
        Button()->GetLayoutObject()->AbsoluteBoundingBoxRect();
    ASSERT_NE(nullptr, status->firstChild()->GetLayoutObject());
    const gfx::Rect marker =
        status->firstChild()->GetLayoutObject()->AbsoluteBoundingBoxRect();
    EXPECT_GT(marker.width(), 0);
    EXPECT_GT(marker.height(), 0);
    EXPECT_TRUE(gfx::Rect(0, 0, 800, 600).Contains(marker));
    if (fixed_slot) {
      ASSERT_NE(nullptr, status->GetLayoutObject());
      const gfx::Rect slot =
          status->GetLayoutObject()->AbsoluteBoundingBoxRect();
      EXPECT_TRUE(slot.Contains(marker));
    }
    if (status_before && !fixed_slot) {
      EXPECT_GT(after.y(), before.y());
    } else {
      EXPECT_EQ(after.y(), before.y());
    }
  }
};

TEST_F(SelectedSemanticActionGeometryProbeTest, StatusBelowAcceptedShape) {
  RunProbe(false, false, false);
}

TEST_F(SelectedSemanticActionGeometryProbeTest, StatusBeforeShiftsButton) {
  RunProbe(true, false, true);
}

TEST_F(SelectedSemanticActionGeometryProbeTest,
       StatusBeforeFixedSizeContainmentKeepsButton) {
  RunProbe(true, true, true);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionPostCaptureMutationSuppressesQueuedTextAndSlots) {
  SetReadyBody("<button id=first aria-label='Original'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  GetElementById("first")->setAttribute(html_names::kAriaLabelAttr,
                                         AtomicString("Changed"));
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionQueuedMutationPreservesCaptureAudit) {
  SetReadyBody("<button id=target aria-label='Visible'></button>"
               "<div contenteditable=true>Editable subtree canary</div>"
               "<button aria-label='Zero canary' style='appearance:none;"
               "width:0;height:20px;padding:0;border:0'></button>"
               "<button aria-label='Offscreen canary' style='position:relative;"
               "left:900px;width:40px;height:20px'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  ASSERT_TRUE(results.empty());
  GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
                                          AtomicString("Changed"));
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_EQ(0u, results[0].reserved_output_bytes);
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
  EXPECT_EQ(1u, results[0].audit.text_reads);
  EXPECT_EQ(0u, results[0].audit.forbidden_reads);
  EXPECT_EQ(7u, results[0].budget.utf8_bytes);
  EXPECT_EQ(1u, results[0].audit.forbidden_structure_prunes);
  EXPECT_EQ(1u, results[0].audit.original_zero_size_exclusions);
  EXPECT_EQ(1u, results[0].audit.original_wholly_offscreen_exclusions);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionStyleOnlyChangeSuppressesQueuedCapture) {
  SetReadyBody("<style id=policy>button{display:block}</style>"
               "<button aria-label='Visible'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  const uint64_t dom_version = GetDocument().DomTreeVersion();
  DummyExceptionStateForTesting exception_state;
  auto* style = To<HTMLStyleElement>(GetElementById("policy"));
  ASSERT_NE(nullptr, style->sheet());
  style->sheet()->insertRule("button{display:none}", 1, exception_state);
  EXPECT_FALSE(exception_state.HadException());
  EXPECT_EQ(dom_version, GetDocument().DomTreeVersion());
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionNestedScrollChangeSuppressesQueuedCapture) {
  SetReadyBody(
      "<div id=scroller style='overflow:scroll;width:150px;height:40px'>"
      "<button aria-label='Visible' style='display:block;position:relative;"
      "top:100px;width:80px;height:20px'></button>"
      "<div style='height:500px'></div></div>");
  auto* box = To<LayoutBox>(GetElementById("scroller")->GetLayoutObject());
  auto* area = box->GetScrollableArea();
  ASSERT_NE(nullptr, area);
  area->SetScrollOffset(ScrollOffset(0, 90),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  const uint64_t dom_version = GetDocument().DomTreeVersion();
  const uint64_t style_version = GetDocument().StyleVersion();
  area->SetScrollOffset(ScrollOffset(0, 0),
                        mojom::blink::ScrollType::kProgrammatic,
                        cc::ScrollSourceType::kNone);
  ASSERT_EQ(ScrollOffset(0, 0), area->GetScrollOffset());
  EXPECT_EQ(dom_version, GetDocument().DomTreeVersion());
  EXPECT_EQ(style_version, GetDocument().StyleVersion());
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionCacheLossSuppressesQueuedCapture) {
  SetReadyBody("<button aria-label='Visible'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  ax_context_.reset();
  ASSERT_EQ(nullptr, GetDocument().ExistingAXObjectCache());
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionDeadlineAfterCaptureSuppressesQueuedSuccess) {
  SetReadyBody("<button aria-label='Visible'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Milliseconds(10),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  task_environment().AdvanceClock(base::Milliseconds(10));
  EXPECT_TRUE(results.empty());
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kDeadlineExceeded,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionQueuedDeadlinePreservesCaptureAudit) {
  SetReadyBody("<button aria-label='Visible'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Milliseconds(10),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  ASSERT_TRUE(results.empty());
  task_environment().AdvanceClock(base::Milliseconds(10));
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kDeadlineExceeded,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_EQ(0u, results[0].reserved_output_bytes);
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
  EXPECT_EQ(1u, results[0].audit.text_reads);
  EXPECT_EQ(0u, results[0].audit.forbidden_reads);
  EXPECT_EQ(7u, results[0].budget.utf8_bytes);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionCompletedImageLayoutSuppressesQueuedCapture) {
  SetReadyBody("<style>html,body{overflow:hidden}</style>"
               "<img id=image style='display:block'>"
               "<button id=target aria-label='Visible' style='display:block;"
               "width:80px;height:20px'></button>");
  auto* view = GetDocument().View();
  ASSERT_NE(nullptr, view);
  auto* layout_viewport = view->GetScrollableArea();
  ASSERT_NE(nullptr, layout_viewport);
  auto& visual_viewport = GetDocument().GetPage()->GetVisualViewport();
  const auto layout_rect =
      layout_viewport->VisibleContentRect(kExcludeScrollbars);
  const auto visual_rect =
      visual_viewport.VisibleContentRect(kExcludeScrollbars);
  const auto layout_offset = layout_viewport->GetScrollOffset();
  const auto visual_offset = visual_viewport.GetScrollOffset();
  const int before_button_y =
      GetElementById("target")->GetLayoutObject()->AbsoluteBoundingBoxRect().y();
  EXPECT_LT(before_button_y, layout_rect.bottom());
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  EXPECT_TRUE(results.empty());
  const uint64_t dom_version = GetDocument().DomTreeVersion();
  const uint64_t style_version = GetDocument().StyleVersion();
  auto* image_layout =
      To<LayoutImage>(GetElementById("image")->GetLayoutObject());
  ASSERT_NE(nullptr, image_layout);
  sk_sp<SkSurface> surface =
      SkSurfaces::Raster(SkImageInfo::MakeN32Premul(40, 900));
  ASSERT_TRUE(surface);
  sk_sp<SkImage> raster = surface->makeImageSnapshot();
  auto* content = ImageResourceContent::CreateLoaded(
      UnacceleratedStaticBitmapImage::Create(raster).get());
  image_layout->ImageResource()->SetImageResource(content);
  UpdateAllLifecyclePhasesForTest();
  const int after_button_y =
      GetElementById("target")->GetLayoutObject()->AbsoluteBoundingBoxRect().y();
  EXPECT_GT(after_button_y, before_button_y);
  EXPECT_GE(after_button_y, layout_rect.bottom());
  EXPECT_EQ(dom_version, GetDocument().DomTreeVersion());
  EXPECT_EQ(style_version, GetDocument().StyleVersion());
  EXPECT_FALSE(view->NeedsLayout());
  EXPECT_EQ(layout_rect,
            layout_viewport->VisibleContentRect(kExcludeScrollbars));
  EXPECT_EQ(visual_rect,
            visual_viewport.VisibleContentRect(kExcludeScrollbars));
  EXPECT_EQ(layout_offset, layout_viewport->GetScrollOffset());
  EXPECT_EQ(visual_offset, visual_viewport.GetScrollOffset());
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionCompletedImageLayoutRevokesDeliveredButtonSlot) {
  SetReadyBody("<style>html,body{overflow:hidden}</style>"
               "<img id=image style='display:block'>"
               "<button aria-label='Visible' style='display:block;"
               "width:80px;height:20px'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  ASSERT_EQ(1u, results[0].entries.size());
  const uint64_t slot = results[0].entries[0].button_slot;
  ASSERT_TRUE(session.HasLiveButtonSlot(slot));
  sk_sp<SkSurface> surface =
      SkSurfaces::Raster(SkImageInfo::MakeN32Premul(40, 900));
  ASSERT_TRUE(surface);
  auto* image_layout =
      To<LayoutImage>(GetElementById("image")->GetLayoutObject());
  ASSERT_NE(nullptr, image_layout);
  image_layout->ImageResource()->SetImageResource(
      ImageResourceContent::CreateLoaded(
          UnacceleratedStaticBitmapImage::Create(
              surface->makeImageSnapshot()).get()));
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(session.HasLiveButtonSlot(slot));
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionPostDeliveryMutationRevokesExistingSlot) {
  SetReadyBody("<button id=first aria-label='Original'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  ASSERT_EQ(1u, results[0].entries.size());
  const auto slot = results[0].entries[0].button_slot;
  ASSERT_TRUE(session.HasLiveButtonSlot(slot));
  GetElementById("first")->setAttribute(html_names::kAriaLabelAttr,
                                         AtomicString("Changed"));
  EXPECT_FALSE(session.HasLiveButtonSlot(slot));
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionChangedLabelCannotStartPress) {
  SetReadyBody("<button id=target aria-label='Original'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> captures;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &captures));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, captures.size());
  ASSERT_EQ(1u, captures[0].entries.size());
  const uint64_t slot = captures[0].entries[0].button_slot;
  GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
                                          AtomicString("Changed"));
  std::vector<WebSelectedSemanticPressV1> presses;
  session.Press(slot, 8, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  Deliver();
  ASSERT_EQ(1u, presses.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kStaleDocument,
            presses[0].status);
  EXPECT_EQ(WebSelectedSemanticActuationStateV1::kNotStarted,
            presses[0].actuation);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionChangedGeometryCannotStartPress) {
  SetReadyBody("<button id=target aria-label='Original' "
               "style='width:100px;height:30px'></button>");
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> captures;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &captures));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, captures.size());
  const uint64_t slot = captures[0].entries[0].button_slot;
  GetElementById("target")->setAttribute(
      html_names::kStyleAttr, AtomicString("width:120px;height:30px"));
  GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kTest);
  std::vector<WebSelectedSemanticPressV1> presses;
  session.Press(slot, 8, base::TimeTicks::Now() + base::Seconds(1),
                base::BindOnce(&RecordWebPress, &presses));
  Deliver();
  ASSERT_EQ(1u, presses.size());
  EXPECT_EQ(WebSelectedSemanticOperationStatusV1::kStaleDocument,
            presses[0].status);
  EXPECT_EQ(WebSelectedSemanticActuationStateV1::kNotStarted,
            presses[0].actuation);
}

TEST_F(SelectedSemanticRequestTest, WebSessionFailedCaptureHasNoPartialSlots) {
  SetReadyBody("<button aria-label='First'></button>"
               "<button id=bad></button>");
  StringBuilder label;
  label.Append("Bad prefix");
  label.Append(UChar(0xd800));
  GetElementById("bad")->setAttribute(html_names::kAriaLabelAttr,
                                      label.ToAtomicString());
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kPolicyResult,
            results[0].terminal);
  EXPECT_EQ(WebSelectedSemanticDispositionV1::kUnsupportedText,
            results[0].disposition);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_FALSE(session.HasLiveButtonSlot(1));
}

TEST_F(SelectedSemanticRequestTest, WebSessionNullDocumentIsClosed) {
  WebSelectedSemanticSession session(
      WebDocument(), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  EXPECT_TRUE(results.empty());
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
}

TEST_F(SelectedSemanticRequestTest, WebSessionMissingCacheIsClosedUnread) {
  SetBodyInnerHTML("<p>Visible</p>");
  ax_context_.reset();
  ASSERT_EQ(nullptr, GetDocument().ExistingAXObjectCache());
  WebSelectedSemanticSession session(
      WebDocument(&GetDocument()), task_environment().GetMainThreadTaskRunner());
  std::vector<WebSelectedSemanticCaptureV1> results;
  session.Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                  base::BindOnce(&RecordWebCapture, &results));
  Deliver();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(WebSelectedSemanticTerminalV1::kStaleContext,
            results[0].terminal);
  EXPECT_TRUE(results[0].entries.empty());
  EXPECT_EQ(0u, results[0].audit.text_reads);
}

TEST_F(SelectedSemanticRequestTest,
       WebSessionDestructionDropsPendingAndQueuedCompletion) {
  for (bool queued : {false, true}) {
    SCOPED_TRACE(queued);
    SetReadyBody("<button aria-label='Visible'></button>");
    auto session = std::make_unique<WebSelectedSemanticSession>(
        WebDocument(&GetDocument()),
        task_environment().GetMainThreadTaskRunner());
    std::vector<WebSelectedSemanticCaptureV1> results;
    session->Capture(7, base::TimeTicks::Now() + base::Seconds(1),
                     base::BindOnce(&RecordWebCapture, &results));
    if (queued)
      DriveAX();
    session.reset();
    DriveAX();
    Deliver();
    EXPECT_TRUE(results.empty());
  }
}

TEST_F(SelectedSemanticRequestTest, RealReadyCallbackPostsImmutableResultAfterFreeze) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  EXPECT_TRUE(results->empty());
  DriveAX();
  EXPECT_TRUE(results->empty());
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(7u, result.epoch);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, result.node.disposition);
  EXPECT_EQ("Allowed", result.node.text);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentLateMalformedLabelClearsEarlierText) {
  SetReadyBody("<p>Allowed first</p><button id=target></button>");
  StringBuilder label;
  label.Append("Bad prefix");
  label.Append(UChar(0xd800));
  GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
                                         label.ToAtomicString());
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestKindV1::kDocument, result.kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(SemanticDispositionV1::kUnsupportedText,
            result.observation.disposition);
  EXPECT_TRUE(result.observation.entries.empty());
  EXPECT_EQ(2u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentCancelClearsEntireResultOnce) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  request->Cancel();
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticRequestKindV1::kDocument, results->front().kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kCancelled, results->front().terminal);
  EXPECT_TRUE(results->front().observation.entries.empty());
  EXPECT_EQ(SemanticDispositionV1::kNotReady,
            results->front().observation.disposition);
  EXPECT_TRUE(results->front().node.text.empty());
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest,
       WholeDocumentBindsOnlyAdmittedButtonsToExactWeakSources) {
  SetReadyBody(
      "<p>Visible text</p>"
      "<button id=first aria-label='Same label'></button>"
      "<button id=second aria-label='Same label'></button>"
      "<div aria-hidden=true>"
      "<button id=excluded aria-label='Excluded label'></button></div>");
  Node* first = GetElementById("first");
  Node* second = GetElementById("second");
  ASSERT_TRUE(first && second);
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  ASSERT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
  ASSERT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(3u, result.observation.entries.size());
  EXPECT_EQ(SemanticObservationRoleV1::kText,
            result.observation.entries[0].role);
  EXPECT_EQ(SemanticObservationRoleV1::kButton,
            result.observation.entries[1].role);
  EXPECT_EQ(SemanticObservationRoleV1::kButton,
            result.observation.entries[2].role);
  EXPECT_EQ("Same label", result.observation.entries[1].text);
  EXPECT_EQ("Same label", result.observation.entries[2].text);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  auto bindings = SelectedSemanticRequestTestPeer::TakeButtonBindings(*request);
  ASSERT_EQ(2u, bindings.size());
  EXPECT_EQ(1u, bindings[0].entry_index);
  EXPECT_EQ(first, bindings[0].node.Get());
  EXPECT_EQ(2u, bindings[1].entry_index);
  EXPECT_EQ(second, bindings[1].node.Get());
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentMissingReadyExpiresUnreadOnce) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  task_environment().FastForwardBy(base::Milliseconds(999));
  EXPECT_TRUE(results->empty());
  task_environment().FastForwardBy(base::Milliseconds(1));
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestKindV1::kDocument, result.kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kDeadlineExceeded, result.terminal);
  EXPECT_TRUE(result.observation.entries.empty());
  EXPECT_EQ(0u, result.audit.text_reads);
  DriveAX();
  Deliver();
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentDestroyPendingIsUnreadOnce) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  request.reset();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticRequestKindV1::kDocument, results->front().kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kCancelled, results->front().terminal);
  EXPECT_TRUE(results->front().observation.entries.empty());
  EXPECT_EQ(0u, results->front().audit.text_reads);
  DriveAX();
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentEpochMismatchAndLateReadyAreUnread) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  request->InvalidateEpoch(8);
  DriveAX();
  SelectedSemanticRequestTestPeer::Ready(*request);
  SelectedSemanticRequestTestPeer::Deadline(*request);
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticRequestKindV1::kDocument, results->front().kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kStaleContext, results->front().terminal);
  EXPECT_EQ(7u, results->front().epoch);
  EXPECT_TRUE(results->front().observation.entries.empty());
  EXPECT_EQ(0u, results->front().audit.text_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentDuplicateReadyCannotCompleteTwice) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  SelectedSemanticRequestTestPeer::Ready(*request);
  SelectedSemanticRequestTestPeer::Deadline(*request);
  request->Cancel();
  request->InvalidateEpoch(8);
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticRequestKindV1::kDocument, results->front().kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, results->front().terminal);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            results->front().observation.disposition);
  ASSERT_EQ(1u, results->front().observation.entries.size());
  EXPECT_EQ("Allowed", results->front().observation.entries[0].text);
  request.reset();
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentCachedGraphFaultsClearPriorText) {
  enum class Fault {
    kCompetingParent, kDuplicateChild, kLateDirtyChild, kWrongLayout
  };
  for (Fault fault : {Fault::kCompetingParent, Fault::kDuplicateChild,
                      Fault::kLateDirtyChild, Fault::kWrongLayout}) {
    SCOPED_TRACE(static_cast<unsigned>(fault));
    SetReadyBody("<p id=first>Allowed first</p><p id=second>Allowed second</p>");
    Node* first = GetElementById("first")->firstChild();
    Node* second = GetElementById("second")->firstChild();
    ASSERT_TRUE(first && second);
    struct Probe {
      std::unique_ptr<SelectedSemanticRequestV1> request;
      bool ran = false;
    };
    auto probe = std::make_shared<Probe>();
    cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> cache,
           WeakPersistent<Node> first, WeakPersistent<Node> second,
           Fault fault, std::shared_ptr<Probe> probe) {
          ASSERT_TRUE(cache && first && second);
          ASSERT_TRUE(cache->IsFrozen());
          ASSERT_TRUE(probe->request);
          AXObject* first_ax = cache->Get(first.Get());
          AXObject* second_ax = cache->Get(second.Get());
          ASSERT_TRUE(first_ax && second_ax);
          AXObject* first_parent = first_ax->ParentObjectIfPresent();
          AXObject* second_parent = second_ax->ParentObjectIfPresent();
          ASSERT_TRUE(first_parent && second_parent);
          ASSERT_NE(first_parent, second_parent);
          ASSERT_TRUE(first_parent->IsIncludedInTree());
          ASSERT_TRUE(second_parent->IsIncludedInTree());
          ASSERT_FALSE(second_ax->NeedsToUpdateCachedValues());
          probe->ran = true;
          if (fault == Fault::kCompetingParent) {
            Member<AXObject> original = SemanticAXCacheStateTestAccess::Parent(*second_ax);
            SemanticAXCacheStateTestAccess::Parent(*second_ax) = first_parent;
            SelectedSemanticRequestTestPeer::Ready(*probe->request);
            SemanticAXCacheStateTestAccess::Parent(*second_ax) = original;
          } else if (fault == Fault::kDuplicateChild) {
            auto& children = SemanticAXCacheStateTestAccess::Children(*second_parent);
            const auto original_size = children.size();
            bool found = false;
            for (const auto& member : children) found |= member.Get() == second_ax;
            ASSERT_TRUE(found);
            children.push_back(second_ax);
            SelectedSemanticRequestTestPeer::Ready(*probe->request);
            children.pop_back();
            EXPECT_EQ(original_size, children.size());
          } else if (fault == Fault::kLateDirtyChild) {
            SemanticAXCacheStateTestAccess::SetDirty(*second_ax, true);
            SelectedSemanticRequestTestPeer::Ready(*probe->request);
            SemanticAXCacheStateTestAccess::SetDirty(*second_ax, false);
          } else {
            LayoutObject* first_layout = first->GetLayoutObject();
            LayoutObject* second_layout = second->GetLayoutObject();
            ASSERT_TRUE(first_layout && second_layout);
            ASSERT_EQ(second_layout, second_ax->GetLayoutObject());
            second->SetLayoutObject(first_layout);
            SelectedSemanticRequestTestPeer::Ready(*probe->request);
            second->SetLayoutObject(second_layout);
          }
        }, cache_, WrapWeakPersistent(first), WrapWeakPersistent(second),
        fault, probe));
    auto results = std::make_shared<Results>();
    probe->request = RequestDocument(results);
    DriveAX();
    ASSERT_TRUE(probe->ran);
    Deliver();
    ASSERT_EQ(1u, results->size());
    const auto& result = results->front();
    EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
    EXPECT_EQ(fault == Fault::kCompetingParent
                  ? SemanticDispositionV1::kStaleDocument
                  : fault == Fault::kDuplicateChild
                        ? SemanticDispositionV1::kLimitExceeded
                        : SemanticDispositionV1::kNotReady,
              result.observation.disposition);
    EXPECT_TRUE(result.observation.entries.empty());
    EXPECT_EQ(fault == Fault::kDuplicateChild ? 2u : 1u,
              result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
  }
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentFlattenedParentAndCycle) {
  for (bool cycle : {false, true}) {
    SCOPED_TRACE(cycle);
    SetReadyBody("<p>Allowed first</p><p><span id=wrapper role=presentation>"
                 "<span id=second>Allowed second</span></span></p>");
    Element* wrapper = GetElementById("wrapper");
    Node* second = GetElementById("second")->firstChild();
    ASSERT_TRUE(wrapper && second);
    struct Probe {
      std::unique_ptr<SelectedSemanticRequestV1> request;
      bool ran = false;
    };
    auto probe = std::make_shared<Probe>();
    cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> cache,
           WeakPersistent<Element> wrapper, WeakPersistent<Node> second,
           bool cycle, std::shared_ptr<Probe> probe) {
          ASSERT_TRUE(cache && wrapper && second && cache->IsFrozen());
          ASSERT_TRUE(probe->request);
          AXObject* ignored = cache->Get(wrapper.Get());
          AXObject* text = cache->Get(second.Get());
          ASSERT_TRUE(ignored && text);
          ASSERT_FALSE(ignored->IsIncludedInTree());
          ASSERT_FALSE(ignored->IsDetached());
          ASSERT_FALSE(ignored->NeedsToUpdateCachedValues());
          bool in_cached_chain = false;
          unsigned count = 0;
          for (const AXObject* parent = text->ParentObjectIfPresent();
               parent && count++ < 16;
               parent = parent->ParentObjectIfPresent()) {
            in_cached_chain |= parent == ignored;
          }
          ASSERT_TRUE(in_cached_chain);
          if (cycle) {
            Member<AXObject> original =
                SemanticAXCacheStateTestAccess::Parent(*ignored);
            SemanticAXCacheStateTestAccess::Parent(*ignored) = ignored;
            SelectedSemanticRequestTestPeer::Ready(*probe->request);
            SemanticAXCacheStateTestAccess::Parent(*ignored) = original;
          } else {
            SelectedSemanticRequestTestPeer::Ready(*probe->request);
          }
          probe->ran = true;
        }, cache_, WrapWeakPersistent(wrapper), WrapWeakPersistent(second),
        cycle, probe));
    auto results = std::make_shared<Results>();
    probe->request = RequestDocument(results);
    DriveAX();
    ASSERT_TRUE(probe->ran);
    Deliver();
    ASSERT_EQ(1u, results->size());
    const auto& result = results->front();
    EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
    EXPECT_EQ(cycle ? SemanticDispositionV1::kStaleDocument
                    : SemanticDispositionV1::kAdmitted,
              result.observation.disposition);
    EXPECT_EQ(cycle ? 0u : 2u, result.observation.entries.size());
    EXPECT_EQ(cycle ? 1u : 2u, result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
  }
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentAbsentWhitespaceSkips) {
  SetReadyBody("<p>Allowed first</p>\n<p>Allowed second</p>");
  Node* gap = GetDocument().body()->firstChild()->nextSibling();
  ASSERT_TRUE(gap && gap->IsTextNode());
  struct Probe {
    std::unique_ptr<SelectedSemanticRequestV1> request;
    bool ran = false;
    bool cached = false;
    bool own_layout_null = false;
    bool ax_layout_null = false;
  };
  auto probe = std::make_shared<Probe>();
  cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> cache,
         WeakPersistent<Node> gap, std::shared_ptr<Probe> probe) {
        ASSERT_TRUE(cache && gap && cache->IsFrozen());
        ASSERT_TRUE(probe->request);
        AXObject* object = cache->Get(gap.Get());
        probe->cached = object != nullptr;
        probe->own_layout_null = !gap->GetLayoutObject();
        probe->ax_layout_null = object && !object->GetLayoutObject();
        SelectedSemanticRequestTestPeer::Ready(*probe->request);
        probe->ran = true;
      }, cache_, WrapWeakPersistent(gap), probe));
  auto results = std::make_shared<Results>();
  probe->request = RequestDocument(results);
  DriveAX();
  ASSERT_TRUE(probe->ran);
  // Blink did not cache this unrendered newline at all. This witnesses AX
  // absence, not the separate both-null cached-object policy branch.
  EXPECT_FALSE(probe->cached);
  EXPECT_TRUE(probe->own_layout_null);
  EXPECT_FALSE(probe->ax_layout_null);
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(2u, result.observation.entries.size());
  EXPECT_EQ("Allowed first", result.observation.entries[0].text);
  EXPECT_EQ("Allowed second", result.observation.entries[1].text);
  EXPECT_EQ(2u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentSourceLessListMarkerSkips) {
  SetReadyBody("<p id=marked style='display:list-item'>Allowed first</p>"
               "<p>Allowed second</p>");
  Element* marked = GetElementById("marked");
  ASSERT_NE(nullptr, marked);
  struct Probe {
    std::unique_ptr<SelectedSemanticRequestV1> request;
    bool ran = false;
    unsigned source_less = 0;
  };
  auto probe = std::make_shared<Probe>();
  cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> cache,
         WeakPersistent<Element> marked, std::shared_ptr<Probe> probe) {
        ASSERT_TRUE(cache && marked && cache->IsFrozen());
        ASSERT_TRUE(probe->request);
        const AXObject* marked_ax = cache->Get(marked.Get());
        ASSERT_NE(nullptr, marked_ax);
        std::vector<const AXObject*> stack{marked_ax};
        while (!stack.empty()) {
          ASSERT_LE(stack.size(), 256u);
          const AXObject* current = stack.back();
          stack.pop_back();
          if (!current->GetNode()) ++probe->source_less;
          for (const auto& member : current->ChildrenIncludingIgnored()) {
            ASSERT_NE(nullptr, member.Get());
            stack.push_back(member.Get());
          }
        }
        SelectedSemanticRequestTestPeer::Ready(*probe->request);
        probe->ran = true;
      }, cache_, WrapWeakPersistent(marked), probe));
  auto results = std::make_shared<Results>();
  probe->request = RequestDocument(results);
  DriveAX();
  ASSERT_TRUE(probe->ran);
  EXPECT_GE(probe->source_less, 1u);
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(2u, result.observation.entries.size());
  EXPECT_EQ("Allowed first", result.observation.entries[0].text);
  EXPECT_EQ("Allowed second", result.observation.entries[1].text);
  EXPECT_EQ(2u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentExcessiveFanoutRejectsWithoutPartialText) {
  StringBuilder html;
  html.Append("<p>Allowed first</p>");
  for (unsigned i = 0; i < 260; ++i)
    html.Append("<button aria-label=Allowed></button>");
  SetReadyBody(html.ToString());
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult,
            results->front().terminal);
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded,
            results->front().observation.disposition);
  EXPECT_TRUE(results->front().observation.entries.empty());
  // The cached body child vector is rejected before the 257th traversal; this
  // is the independent per-parent fanout guard, not a distinct-source proof.
  EXPECT_EQ(3u, results->front().budget.nodes);
  EXPECT_LT(results->front().budget.fragments, 512u);
  EXPECT_LT(results->front().budget.relation_steps, 4096u);
  EXPECT_EQ(0u, results->front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentDistinctCachedSourcesReachLimit) {
  StringBuilder html;
  html.Append("<p>Allowed first</p>");
  for (unsigned i = 0; i < 255; ++i)
    html.Append("<div role=group></div>");
  SetReadyBody(html.ToString());
  ASSERT_EQ(256u, GetDocument().body()->CountChildren());
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(256u, result.budget.nodes);
  EXPECT_LT(result.budget.relation_steps, 4096u);
  EXPECT_LT(result.budget.fragments, 512u);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded,
            result.observation.disposition);
  EXPECT_TRUE(result.observation.entries.empty());
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentAcceptsExactDistinctSourceLimit) {
  StringBuilder html;
  html.Append("<p>Allowed first</p>");
  for (unsigned i = 0; i < 251; ++i)
    html.Append("<div role=group></div>");
  SetReadyBody(html.ToString());
  ASSERT_EQ(252u, GetDocument().body()->CountChildren());
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
  EXPECT_EQ(256u, result.budget.nodes);
  EXPECT_LT(result.budget.relation_steps, 4096u);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(1u, result.observation.entries.size());
  EXPECT_EQ("Allowed first", result.observation.entries[0].text);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentDeepTreeWorkRejectsFirst) {
  StringBuilder html;
  for (unsigned i = 0; i < 65; ++i) html.Append("<div role=group>");
  html.Append("<button aria-label=Allowed></button>");
  for (unsigned i = 0; i < 65; ++i) html.Append("</div>");
  SetReadyBody(html.ToString());
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded,
            results->front().observation.disposition);
  EXPECT_TRUE(results->front().observation.entries.empty());
  EXPECT_LT(results->front().budget.depth, 64u);
  EXPECT_LT(results->front().budget.nodes, 256u);
  EXPECT_EQ(4096u, results->front().budget.relation_steps);
  EXPECT_EQ(0u, results->front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentSharedRelationWorkRejects) {
  StringBuilder html;
  html.Append("<p>Allowed first</p>");
  for (unsigned depth = 0; depth < 16; ++depth) {
    html.Append("<div role=group");
    for (unsigned attribute = 0; attribute < 80; ++attribute) {
      html.Append(" data-k");
      html.AppendNumber(attribute);
      html.Append("=x");
    }
    html.Append(">");
  }
  html.Append("<button aria-label=Allowed></button>");
  for (unsigned depth = 0; depth < 16; ++depth) html.Append("</div>");
  SetReadyBody(html.ToString());
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded,
            results->front().observation.disposition);
  EXPECT_TRUE(results->front().observation.entries.empty());
  EXPECT_LE(results->front().budget.relation_steps, 4096u);
  EXPECT_GT(80u, 4096u - results->front().budget.relation_steps);
  EXPECT_LT(results->front().budget.nodes, 256u);
  EXPECT_LT(results->front().budget.fragments, 512u);
  EXPECT_GE(results->front().audit.text_reads, 1u);
  EXPECT_EQ(0u, results->front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentAggregatesFragmentBudget) {
  for (unsigned second_count : {24u, 25u}) {
    SCOPED_TRACE(second_count);
    StringBuilder html;
    html.Append("<p id=first style='white-space:pre'>");
    for (unsigned i = 0; i < 24; ++i)
      html.Append(static_cast<UChar>((i & 1) ? 0x05D0 : 'A'));
    html.Append("</p><p id=second style='white-space:pre'>");
    for (unsigned i = 0; i < second_count; ++i)
      html.Append(static_cast<UChar>((i & 1) ? 0x05D0 : 'A'));
    html.Append("</p>");
    SetReadyBody(html.ToString());
    auto* first = DynamicTo<LayoutText>(
        GetElementById("first")->firstChild()->GetLayoutObject());
    auto* second = DynamicTo<LayoutText>(
        GetElementById("second")->firstChild()->GetLayoutObject());
    ASSERT_TRUE(first && second);
    ASSERT_TRUE(first->HasValidInlineItems());
    ASSERT_TRUE(second->HasValidInlineItems());
    ASSERT_EQ(24u, first->InlineItems().size());
    ASSERT_EQ(second_count, second->InlineItems().size());
    auto results = std::make_shared<Results>();
    auto request = RequestDocument(results);
    DriveAX();
    Deliver();
    ASSERT_EQ(1u, results->size());
    const auto& result = results->front();
    EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
    EXPECT_EQ(24u + second_count, result.budget.fragments);
    EXPECT_LT(result.budget.relation_steps, 4096u);
    EXPECT_LT(result.budget.nodes, 256u);
    EXPECT_EQ(SemanticDispositionV1::kAdmitted,
              result.observation.disposition);
    ASSERT_EQ(2u, result.observation.entries.size());
    EXPECT_EQ(2u, result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
  }
}

TEST_F(SelectedSemanticRequestTest, OwnFragmentCounterAccepts512AndRejects513) {
  SetReadyBody("<p id=target>Allowed</p>");
  Node* target = Target();
  auto* layout = DynamicTo<LayoutText>(target->GetLayoutObject());
  ASSERT_NE(nullptr, layout);
  ASSERT_EQ(1u, layout->InlineItems().size());
  struct Probe {
    bool ran = false;
    SemanticDispositionV1 exact = SemanticDispositionV1::kNotReady;
    SemanticDispositionV1 over = SemanticDispositionV1::kNotReady;
    SemanticBudgetV1 exact_budget;
    SemanticBudgetV1 over_budget;
  };
  auto probe = std::make_shared<Probe>();
  cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> cache, WeakPersistent<Node> target,
         std::shared_ptr<Probe> probe) {
        ASSERT_TRUE(cache && target && cache->IsFrozen());
        ScriptForbiddenScope forbid_script;
        AXObject* object = cache->Get(target.Get());
        ASSERT_NE(nullptr, object);
        probe->exact_budget.fragments = 511;
        probe->over_budget.fragments = 512;
        probe->exact = ClassifyOwnGeometryV1(*object, probe->exact_budget);
        probe->over = ClassifyOwnGeometryV1(*object, probe->over_budget);
        probe->ran = true;
      }, cache_, WrapWeakPersistent(target), probe));
  DriveAX();
  ASSERT_TRUE(probe->ran);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, probe->exact);
  EXPECT_EQ(512u, probe->exact_budget.fragments);
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded, probe->over);
  EXPECT_EQ(512u, probe->over_budget.fragments);
}

TEST_F(SelectedSemanticRequestTest, StructureDepthAccepts64AndRejects65) {
  for (unsigned wrappers : {59u, 60u}) {
    SCOPED_TRACE(wrappers);
    StringBuilder html;
    for (unsigned i = 0; i < wrappers; ++i) html.Append("<div>");
    html.Append("<p id=target>Allowed</p>");
    for (unsigned i = 0; i < wrappers; ++i) html.Append("</div>");
    SetReadyBody(html.ToString());
    Node* target = Target();
    struct Probe {
      bool ran = false;
      SemanticDispositionV1 disposition = SemanticDispositionV1::kNotReady;
      SemanticBudgetV1 budget;
      SemanticAuditV1 audit;
    };
    auto probe = std::make_shared<Probe>();
    cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> cache, WeakPersistent<Node> target,
           std::shared_ptr<Probe> probe) {
          ASSERT_TRUE(cache && target && cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          probe->disposition = ClassifySelectedStructureV1(
              *target, *cache, probe->budget, probe->audit);
          probe->ran = true;
        }, cache_, WrapWeakPersistent(target), probe));
    DriveAX();
    ASSERT_TRUE(probe->ran);
    EXPECT_EQ(64u, probe->budget.depth);
    EXPECT_LT(probe->budget.nodes, 256u);
    EXPECT_LT(probe->budget.relation_steps, 4096u);
    EXPECT_EQ(wrappers == 59 ? SemanticDispositionV1::kAdmitted
                             : SemanticDispositionV1::kLimitExceeded,
              probe->disposition);
    EXPECT_EQ(0u, probe->audit.text_reads);
    EXPECT_EQ(0u, probe->audit.forbidden_reads);
  }
}

TEST_F(SelectedSemanticRequestTest, RelationshipCounterAccepts4096AndRejects4097) {
  SetReadyBody();
  struct Probe {
    bool ran = false;
    SemanticDispositionV1 exact = SemanticDispositionV1::kNotReady;
    SemanticDispositionV1 over = SemanticDispositionV1::kNotReady;
    SemanticBudgetV1 exact_budget;
    SemanticBudgetV1 over_budget;
  };
  auto probe = std::make_shared<Probe>();
  cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> cache,
         WeakPersistent<Document> document, std::shared_ptr<Probe> probe) {
        ASSERT_TRUE(cache && document && cache->IsFrozen());
        ScriptForbiddenScope forbid_script;
        SemanticAuditV1 audit;
        probe->exact_budget.relation_steps = 4095;
        probe->over_budget.relation_steps = 4096;
        probe->exact = ClassifySelectedStructureV1(
            *document, *cache, probe->exact_budget, audit);
        probe->over = ClassifySelectedStructureV1(
            *document, *cache, probe->over_budget, audit);
        EXPECT_EQ(0u, audit.text_reads);
        EXPECT_EQ(0u, audit.forbidden_reads);
        probe->ran = true;
      }, cache_, WrapWeakPersistent(&GetDocument()), probe));
  DriveAX();
  ASSERT_TRUE(probe->ran);
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, probe->exact);
  EXPECT_EQ(4096u, probe->exact_budget.relation_steps);
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded, probe->over);
  EXPECT_EQ(4096u, probe->over_budget.relation_steps);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentLateLongLabelClearsEarlierText) {
  SetReadyBody("<p>Allowed first</p><button id=target></button>");
  GetElementById("target")->setAttribute(html_names::kAriaLabelAttr,
      AtomicString(String::FromUtf8(std::string(4097, 'x'))));
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticDispositionV1::kLimitExceeded,
            results->front().observation.disposition);
  EXPECT_TRUE(results->front().observation.entries.empty());
  EXPECT_EQ(2u, results->front().audit.text_reads);
  EXPECT_EQ(0u, results->front().audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentPrunesEditableAndHiddenText) {
  SetReadyBody(
      "<p>Allowed first</p>"
      "<div contenteditable=true><p>Editable canary</p></div>"
      "<div aria-hidden=true><p>Hidden canary</p></div>"
      "<p>Allowed second</p>");
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(2u, result.observation.entries.size());
  EXPECT_EQ("Allowed first", result.observation.entries[0].text);
  EXPECT_EQ("Allowed second", result.observation.entries[1].text);
  EXPECT_EQ(2u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentTraversesZeroSizeContainer) {
  SetReadyBody(
      "<div style='width:0;height:0;overflow:visible'>"
      "<p style='width:150px;height:20px'>Allowed visible child</p></div>");
  auto* container = GetDocument().body()->firstElementChild();
  ASSERT_NE(nullptr, container);
  auto* box = DynamicTo<LayoutBox>(container->GetLayoutObject());
  ASSERT_NE(nullptr, box);
  const auto* fragment = box->GetPhysicalFragment(0);
  ASSERT_NE(nullptr, fragment);
  ASSERT_EQ(LayoutUnit(), fragment->Size().width);
  ASSERT_EQ(LayoutUnit(), fragment->Size().height);
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(1u, result.observation.entries.size());
  EXPECT_EQ("Allowed visible child", result.observation.entries[0].text);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentExcludesOwnZeroAndOffscreenLeaves) {
  SetReadyBody(
      "<p>Allowed first</p>"
      "<button aria-label='Zero canary' style='appearance:none;width:0;"
      "height:20px;padding:0;border:0;overflow:visible'></button>"
      "<button aria-label='Offscreen canary' style='position:relative;"
      "left:900px;width:40px;height:20px'></button>"
      "<p>Allowed second</p>");
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(2u, result.observation.entries.size());
  EXPECT_EQ("Allowed first", result.observation.entries[0].text);
  EXPECT_EQ("Allowed second", result.observation.entries[1].text);
  EXPECT_EQ(2u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentEmptyDirectLabelIsLocalSkip) {
  SetReadyBody("<p>Allowed first</p>"
               "<button aria-label='' style='width:60px;height:20px'></button>"
               "<p>Allowed second</p>");
  auto results = std::make_shared<Results>();
  auto request = RequestDocument(results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticDispositionV1::kAdmitted,
            result.observation.disposition);
  ASSERT_EQ(2u, result.observation.entries.size());
  EXPECT_EQ("Allowed first", result.observation.entries[0].text);
  EXPECT_EQ("Allowed second", result.observation.entries[1].text);
  EXPECT_EQ(3u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentOutputReservationExactAndOver) {
  StringBuilder html;
  for (unsigned i = 0; i < 16; ++i)
    html.Append("<button style='appearance:none;width:60px;height:20px'>"
                "</button>");
  for (unsigned last_length : {2816u, 2817u}) {
    SetReadyBody(html.ToString());
    auto* button = GetDocument().body()->firstElementChild();
    for (unsigned i = 0; i < 16; ++i) {
      ASSERT_NE(nullptr, button);
      const unsigned length = i == 15 ? last_length : 4096;
      button->setAttribute(html_names::kAriaLabelAttr,
          AtomicString(String::FromUtf8(std::string(length, 'x'))));
      button = button->nextElementSibling();
    }
    ASSERT_EQ(nullptr, button);
    auto results = std::make_shared<Results>();
    auto request = RequestDocument(results);
    DriveAX();
    Deliver();
    ASSERT_EQ(1u, results->size());
    const auto& result = results->front();
    if (last_length == 2816) {
      EXPECT_EQ(SemanticDispositionV1::kAdmitted,
                result.observation.disposition);
      EXPECT_EQ(16u, result.observation.entries.size());
      EXPECT_EQ(65536u, result.observation.reserved_output_bytes);
      EXPECT_EQ(64256u, result.budget.utf8_bytes);
    } else {
      EXPECT_EQ(SemanticDispositionV1::kLimitExceeded,
                result.observation.disposition);
      EXPECT_TRUE(result.observation.entries.empty());
      EXPECT_EQ(0u, result.observation.reserved_output_bytes);
    }
    EXPECT_EQ(16u, result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
  }
}

TEST_F(SelectedSemanticRequestTest, OwnZeroAndOffscreenRequestsStayUnread) {
  const char* styles[] = {
      "appearance:none;width:0;height:20px;padding:0;border:0;overflow:visible",
      "position:relative;left:900px;width:40px;height:20px"};
  for (unsigned index = 0; index < std::size(styles); ++index) {
    SCOPED_TRACE(index);
    SetReadyBody(String("<button id=target aria-label=Excluded style='") +
                 styles[index] + "'></button>");
    auto results = std::make_shared<Results>();
    auto request = Request(*GetElementById("target"), results);
    DriveAX();
    EXPECT_TRUE(results->empty());
    Deliver();
    ASSERT_EQ(1u, results->size());
    const auto& result = results->front();
    ExpectUnread(result, SemanticRequestTerminalV1::kPolicyResult);
    EXPECT_EQ(index == 0 ? SemanticDispositionV1::kOwnZeroSize
                        : SemanticDispositionV1::kOffscreen,
              result.node.disposition);
    EXPECT_EQ(0u, result.budget.utf8_bytes);
  }
}

TEST_F(SelectedSemanticRequestTest, CancelBeforeCallbackIsUnreadAndOnce) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  request->Cancel();
  request->Cancel();
  EXPECT_TRUE(results->empty());
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kCancelled);
  DriveAX();
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, EpochMismatchBeforeCallbackIsUnread) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  request->InvalidateEpoch(8);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kStaleContext);
  EXPECT_EQ(7u, results->front().epoch);
}

TEST_F(SelectedSemanticRequestTest, EqualEpochDoesNotCancel) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  request->InvalidateEpoch(7);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, results->front().node.disposition);
}

TEST_F(SelectedSemanticRequestTest, MissingCallbackExpiresWithoutForcedAX) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  // The RenderingTest ChromeClient records scheduling but does not drive a
  // frame. Advancing the base timer alone cannot force an AX capture.
  task_environment().FastForwardBy(base::Milliseconds(999));
  EXPECT_TRUE(results->empty());
  task_environment().FastForwardBy(base::Milliseconds(1));
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kDeadlineExceeded);
  DriveAX();
  Deliver();
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, DestroyPendingRequestPostsOneCancellation) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  request.reset();
  EXPECT_TRUE(results->empty());
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kCancelled);
  DriveAX();
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, DuplicateAndLateHandlersCannotCompleteTwice) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  DriveAX();
  // Invoke handlers through the test peer, never run a consumed OnceClosure.
  SelectedSemanticRequestTestPeer::Ready(*request);
  SelectedSemanticRequestTestPeer::Deadline(*request);
  request->Cancel();
  request->InvalidateEpoch(8);
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, results->front().node.disposition);
  request.reset();
  Deliver();
  EXPECT_EQ(1u, results->size());
}

TEST_F(SelectedSemanticRequestTest, DeadlineAndEpochAdmissionCannotBeWidened) {
  SetReadyBody();
  for (const auto duration : {base::Milliseconds(0), base::Milliseconds(5001)}) {
    auto results = std::make_shared<Results>();
    auto request = Request(*Target(), results, 7, duration);
    Deliver();
    ASSERT_EQ(1u, results->size());
    ExpectUnread(results->front(), duration.is_zero()
        ? SemanticRequestTerminalV1::kDeadlineExceeded
        : SemanticRequestTerminalV1::kInvalidRequest);
  }
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results, 0);
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kInvalidRequest);
}

TEST_F(SelectedSemanticRequestTest, FiveSecondDeadlineBoundaryIsAccepted) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results, 7, base::Seconds(5));
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ(SemanticDispositionV1::kAdmitted, results->front().node.disposition);
}

TEST_F(SelectedSemanticRequestTest, DetachedTargetCannotBeReboundById) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  Persistent<Node> old_target(Target());
  Persistent<AXObject> old_object(cache_->Get(old_target.Get()));
  ASSERT_NE(nullptr, old_object.Get());
  auto request = Request(*old_target, results);
  GetElementById("target")->SetInnerHTMLWithoutTrustedTypes("Replacement");
  ASSERT_NE(old_target.Get(), Target());
  ASSERT_FALSE(old_target->isConnected());
  DriveAX();
  EXPECT_TRUE(old_object->IsDetached());
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kStaleContext);
}

TEST_F(SelectedSemanticRequestTest, MissingRealAXObjectFailsWithoutCreation) {
  SetReadyBody("<script type=application/json id=target>Excluded</script>");
  ASSERT_EQ(nullptr, cache_->Get(Target()));
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  DriveAX();
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kPolicyResult);
  EXPECT_EQ(SemanticDispositionV1::kStaleDocument, results->front().node.disposition);
  EXPECT_EQ(nullptr, cache_->Get(Target()));
}

TEST_F(SelectedSemanticRequestTest, NonFrozenDirtyHandlerCannotPrepareReadiness) {
  SetReadyBody();
  auto results = std::make_shared<Results>();
  auto request = Request(*Target(), results);
  GetElementById("target")->setAttribute(html_names::kStyleAttr, AtomicString("color:red"));
  const auto state = GetDocument().Lifecycle().GetState();
  ASSERT_LT(state, DocumentLifecycle::kPrePaintClean);
  SelectedSemanticRequestTestPeer::Ready(*request);
  EXPECT_EQ(state, GetDocument().Lifecycle().GetState());
  Deliver();
  ASSERT_EQ(1u, results->size());
  ExpectUnread(results->front(), SemanticRequestTerminalV1::kPolicyResult);
  EXPECT_EQ(SemanticDispositionV1::kNotReady, results->front().node.disposition);
  DriveAX();
  Deliver();
  EXPECT_EQ(1u, results->size());
}

// A request-local monotonic clock; never alters TaskEnvironment/global clocks.
// Admission and timer arming see before_. After Arm(), only the first read sees
// before_; all later reads see deadline_. Both methods are thread-safe, although
// this test arms and reads it solely on the renderer main thread.
class SelectedSemanticRequestSteppingClock final : public base::TickClock {
 public:
  SelectedSemanticRequestSteppingClock(base::TimeTicks before,
                                      base::TimeTicks deadline)
      : before_(before), deadline_(deadline) {
    CHECK(before_ < deadline_);
  }
  base::TimeTicks NowTicks() const override {
    if (!armed_.load()) return before_;
    return reads_after_arm_.fetch_add(1) == 0 ? before_ : deadline_;
  }
  void Arm() { CHECK(!armed_.exchange(true)); }
  unsigned ReadsAfterArm() const { return reads_after_arm_.load(); }

 private:
  const base::TimeTicks before_;
  const base::TimeTicks deadline_;
  std::atomic<bool> armed_{false};
  mutable std::atomic<unsigned> reads_after_arm_{0};
};

TEST_F(SelectedSemanticRequestTest, PostReadDeadlineDropsTextButPreservesAudit) {
  ASSERT_NO_FATAL_FAILURE(SetReadyBody());
  auto results = std::make_shared<Results>();
  const auto before = base::TimeTicks::Now();
  const auto deadline = before + base::Seconds(1);
  struct Probe {
    // Reverse destruction order keeps the clock alive through request/timer
    // destruction, including early assertion failure and queued callback drop.
    std::shared_ptr<SelectedSemanticRequestSteppingClock> clock;
    std::unique_ptr<SelectedSemanticRequestV1> request;
    bool ready_ran = false;
  };
  auto probe = std::make_shared<Probe>();
  probe->clock = std::make_shared<SelectedSemanticRequestSteppingClock>(
      before, deadline);

  // Queue first: this real frozen callback must run before CreateWithClock's
  // ordinary ready closure. Otherwise the request could succeed before this
  // test arms the stepping clock. Captured state owns its own safe lifetime.
  cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> cache, std::shared_ptr<Probe> probe,
         std::shared_ptr<Results> results) {
        ASSERT_TRUE(cache);
        ASSERT_TRUE(cache->IsFrozen());
        ASSERT_TRUE(probe->request);
        probe->ready_ran = true;
        probe->clock->Arm();
        SelectedSemanticRequestTestPeer::Ready(*probe->request);
        // Exactly the pre-read and post-read request checks used this clock;
        // a scheduler/global-time read cannot consume its stepping sequence.
        EXPECT_EQ(2u, probe->clock->ReadsAfterArm());
        EXPECT_TRUE(results->empty());
      }, cache_, probe, results));

  probe->request = SelectedSemanticRequestTestPeer::CreateWithClock(
      GetDocument(), *cache_, *Target(), 7, deadline,
      task_environment().GetMainThreadTaskRunner(),
      blink::BindOnce(
          [](WeakPersistent<AXObjectCacheImpl> cache,
             std::shared_ptr<Results> results, SemanticRequestResultV1 result) {
            EXPECT_FALSE(cache && cache->IsFrozen());
            results->push_back(std::move(result));
          }, cache_, results),
      *probe->clock);
  EXPECT_TRUE(results->empty());
  ASSERT_NO_FATAL_FAILURE(DriveAX());
  ASSERT_TRUE(probe->ready_ran);
  EXPECT_TRUE(results->empty());
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestTerminalV1::kDeadlineExceeded, result.terminal);
  EXPECT_EQ(7u, result.epoch);
  EXPECT_TRUE(result.node.text.empty());
  EXPECT_EQ(SemanticDispositionV1::kNotReady, result.node.disposition);
  EXPECT_EQ(1u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.structural_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  EXPECT_EQ(7u, result.budget.utf8_bytes);  // The real "Allowed" read occurred.

  // Terminal commit invalidated the ordinary queued ready closure and stopped
  // the timer. Later handlers/destruction cannot publish another completion.
  SelectedSemanticRequestTestPeer::Ready(*probe->request);
  SelectedSemanticRequestTestPeer::Deadline(*probe->request);
  probe->request.reset();
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
  EXPECT_EQ(2u, probe->clock->ReadsAfterArm());
}

TEST_F(SelectedSemanticRequestTest, WholeDocumentPostReadExpiryClearsEntries) {
  SetReadyBody("<p>Allowed first</p><p>Allowed second</p>");
  auto results = std::make_shared<Results>();
  const auto before = base::TimeTicks::Now();
  const auto deadline = before + base::Seconds(1);
  struct Probe {
    std::shared_ptr<SelectedSemanticRequestSteppingClock> clock;
    std::unique_ptr<SelectedSemanticRequestV1> request;
    bool ready_ran = false;
  };
  auto probe = std::make_shared<Probe>();
  probe->clock = std::make_shared<SelectedSemanticRequestSteppingClock>(
      before, deadline);
  cache_->ScheduleAXUpdateWithCallback(blink::BindOnce(
      [](WeakPersistent<AXObjectCacheImpl> cache, std::shared_ptr<Probe> probe) {
        ASSERT_TRUE(cache && cache->IsFrozen());
        ASSERT_TRUE(probe->request);
        probe->ready_ran = true;
        probe->clock->Arm();
        SelectedSemanticRequestTestPeer::Ready(*probe->request);
        EXPECT_EQ(2u, probe->clock->ReadsAfterArm());
      }, cache_, probe));
  probe->request = SelectedSemanticRequestTestPeer::CreateWithClock(
      GetDocument(), *cache_, GetDocument(), 7, deadline,
      task_environment().GetMainThreadTaskRunner(),
      blink::BindOnce(
          [](WeakPersistent<AXObjectCacheImpl> cache,
             std::shared_ptr<Results> results, SemanticRequestResultV1 result) {
            EXPECT_FALSE(cache && cache->IsFrozen());
            results->push_back(std::move(result));
          }, cache_, results),
      *probe->clock, SemanticRequestKindV1::kDocument);
  DriveAX();
  ASSERT_TRUE(probe->ready_ran);
  Deliver();
  ASSERT_EQ(1u, results->size());
  const auto& result = results->front();
  EXPECT_EQ(SemanticRequestKindV1::kDocument, result.kind);
  EXPECT_EQ(SemanticRequestTerminalV1::kDeadlineExceeded, result.terminal);
  EXPECT_EQ(SemanticDispositionV1::kNotReady,
            result.observation.disposition);
  EXPECT_TRUE(result.observation.entries.empty());
  EXPECT_EQ(0u, result.observation.reserved_output_bytes);
  EXPECT_EQ(2u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  EXPECT_EQ(27u, result.budget.utf8_bytes);
  SelectedSemanticRequestTestPeer::Ready(*probe->request);
  SelectedSemanticRequestTestPeer::Deadline(*probe->request);
  probe->request.reset();
  task_environment().FastForwardBy(base::Seconds(2));
  EXPECT_EQ(1u, results->size());
}


namespace {
// action.html SHA256 da2b79e69a9f165533125a5f046ca28bc3b1de680f7d35a1d143e9c94a60c473
constexpr char kRun12Action[] = R"LUNARHTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <link rel="icon" href="data:,">
  <title>Lunar I2-07 feasibility action</title>
  <style nonce="cpW9Rox9tcvrPqZNoQGvsWZ+">
    html,body{margin:0;width:100%;height:100%;overflow:hidden}main{box-sizing:border-box;height:100%;padding:4px;font:10px/12px sans-serif}.fixture-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:2px 8px}.fixture-case{display:grid;grid-template-rows:20px 20px;height:40px;align-items:start}.fixture-case>*{box-sizing:border-box;margin:0;min-width:0;max-width:100%}.fixture-case>*:first-child{height:20px;overflow:hidden}.fixture-case>p:last-child{line-height:10px;white-space:normal;overflow:visible}.selector-image{width:16px;height:16px}
    .display-none { display: none; }
    iframe { width: 8px; height: 8px; border: 0; }
  </style>
</head>
<body data-complete="lunar-action:complete:run-12">
  <main>
    <p>lunar-document:safe:run-12:revision-1</p>
    <p>lunar-feasibility-visible-canary:run-12</p>
    <button id="allowed-action" aria-label="Activate controlled fixture action"></button>
    <p id="action-status">lunar-action:pending:run-12</p>

    <section class="fixture-grid" aria-label="structural exclusion cases">
      <div class="fixture-case" role="group" aria-label="Structural case display-none" data-case="display-none"><p class="display-none">lunar-feasibility-excluded:display-none:run-12</p><p>lunar-feasibility-anchor:display-none:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case aria-hidden" data-case="aria-hidden"><p aria-hidden="true">lunar-feasibility-excluded:aria-hidden:run-12</p><p>lunar-feasibility-anchor:aria-hidden:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case disabled-control" data-case="disabled-control"><input type="checkbox" disabled aria-label="Disabled structural control" data-canary="lunar-feasibility-excluded:disabled-control:run-12"><p>lunar-feasibility-anchor:disabled-control:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case protected" data-case="protected"><input type="password" aria-label="Protected fixture field" value="lunar-feasibility-excluded:protected:run-12"><p>lunar-feasibility-anchor:protected:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case ordinary-input" data-case="ordinary-input"><input type="text" aria-label="Ordinary fixture field" value="lunar-feasibility-excluded:ordinary-input:run-12"><p>lunar-feasibility-anchor:ordinary-input:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case password-input" data-case="password-input"><input type="password" aria-label="Password fixture field" value="lunar-feasibility-excluded:password-input:run-12"><p>lunar-feasibility-anchor:password-input:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case contenteditable-heading" data-case="contenteditable-heading"><h2 contenteditable="true">lunar-feasibility-excluded:contenteditable-heading:run-12</h2><p>lunar-feasibility-anchor:contenteditable-heading:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case contenteditable-group" data-case="contenteditable-group"><div role="group" contenteditable="true">lunar-feasibility-excluded:contenteditable-group:run-12</div><p>lunar-feasibility-anchor:contenteditable-group:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case nested-frame" data-case="nested-frame"><iframe src="/nested-frame" title="lunar-feasibility-excluded:nested-frame:run-12"></iframe><p>lunar-feasibility-anchor:nested-frame:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case content-named-button" data-case="content-named-button"><button>lunar-feasibility-excluded:content-named-button:run-12</button><p>lunar-feasibility-anchor:content-named-button:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case empty-explicit-label" data-case="empty-explicit-label"><button aria-label="" data-canary="lunar-feasibility-excluded:empty-explicit-label:run-12"></button><p>lunar-feasibility-anchor:empty-explicit-label:run-12</p></div>
      <div class="fixture-case" role="group" aria-label="Structural case unsupported-selector" data-case="unsupported-selector"><div role="img" aria-label="Unavailable selector image" class="selector-image" data-canary="lunar-feasibility-excluded:unsupported-selector:run-12"></div><p>lunar-feasibility-anchor:unsupported-selector:run-12</p></div>
    </section>
  </main>
  <script nonce="cpW9Rox9tcvrPqZNoQGvsWZ+">document.getElementById("allowed-action").addEventListener("click",()=>{document.getElementById("action-status").firstChild.data=document.body.dataset.complete;});</script>
</body>
</html>
)LUNARHTML";

// action-noop.html SHA256 d9baa3167030204225872971d5623361953a7b99aa4745eccc0a8fcebc0fb483
[[maybe_unused]] constexpr char kRun12ActionNoop[] = R"LUNARHTML(<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><link rel="icon" href="data:,"><title>Lunar I2-07 feasibility no-op</title></head>
<body data-complete="lunar-action:complete:run-12">
  <main>
    <p>lunar-document:noop:run-12:revision-1</p>
    <p>lunar-feasibility-visible-canary:run-12</p>
    <button id="allowed-action" aria-label="Activate controlled fixture action"></button>
    <p id="action-status">lunar-action:pending:run-12</p>
  </main>
</body>
</html>
)LUNARHTML";

// reload-old.html SHA256 2e38b262b3a657aef99d7d358acb28e9480174d41106cf8646181bdba0f173ec
[[maybe_unused]] constexpr char kRun12ReloadOld[] = R"LUNARHTML(<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><link rel="icon" href="data:,"><title>Lunar I2-07 feasibility reload old</title></head>
<body data-complete="lunar-action:complete:run-12">
  <main>
    <p>lunar-document:reload-old:run-12:revision-1</p>
    <p>lunar-feasibility-visible-canary:run-12</p>
    <button id="allowed-action" aria-label="Activate controlled fixture action"></button>
    <p id="action-status">lunar-action:pending:run-12</p>
  </main>
</body>
</html>
)LUNARHTML";

// reload-new.html SHA256 5635f2c6f5a8295fc97446fffd771e3245a0e897c47a0ad1a42d680dfbf9c036
[[maybe_unused]] constexpr char kRun12ReloadNew[] = R"LUNARHTML(<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><link rel="icon" href="data:,"><title>Lunar I2-07 feasibility reload new</title></head>
<body data-complete="lunar-action:complete:run-12">
  <main>
    <p>lunar-document:reload-new:run-12:revision-2</p>
    <p>lunar-feasibility-visible-canary:run-12</p>
    <button id="allowed-action" aria-label="Activate controlled fixture action"></button>
    <p id="action-status">lunar-action:pending:run-12</p>
  </main>
</body>
</html>
)LUNARHTML";

// nested-frame.html SHA256 d1bb91eb3075b1a489baf545270f6fe1d3e7fdbf4dcbad27879d1fe4041f9647
constexpr char kRun12NestedFrame[] = R"LUNARHTML(<!doctype html><html><body><p>nested-frame-canary</p></body></html>
)LUNARHTML";

constexpr const char* kRun12Cases[] = {
    "display-none", "aria-hidden", "disabled-control", "protected",
    "ordinary-input", "password-input", "contenteditable-heading",
    "contenteditable-group", "nested-frame", "content-named-button",
    "empty-explicit-label", "unsupported-selector"};

// Separate minimal-fixture mutation helper, never used by a semantic capture.
// Blink invokes this through actual ResizeObserver lifecycle delivery only.
class SemanticTargetDetachingResizeDelegate final
    : public ResizeObserver::Delegate {
 public:
  SemanticTargetDetachingResizeDelegate(Node* target, AXObjectCacheImpl* cache)
      : target_(target), cache_(cache) {}
  void OnResize(const HeapVector<Member<ResizeObserverEntry>>& entries) override {
    ++calls_;
    EXPECT_FALSE(entries.empty());
    EXPECT_FALSE(cache_ && cache_->IsFrozen());
    if (removed_) return;
    // Hold only for this test mutation. The request itself keeps weak handles.
    Persistent<Node> target(target_.Get());
    if (!target || !target->isConnected()) return;
    target->remove();
    removed_ = !target->isConnected();
  }
  unsigned Calls() const { return calls_; }
  bool Removed() const { return removed_; }
  void Trace(Visitor* visitor) const override {
    ResizeObserver::Delegate::Trace(visitor);
    visitor->Trace(target_);
    visitor->Trace(cache_);
  }

 private:
  WeakMember<Node> target_;
  WeakMember<AXObjectCacheImpl> cache_;
  unsigned calls_ = 0;
  bool removed_ = false;
};

class SelectedSemanticRun12FixtureTest : public SimTest {
 protected:
  SelectedSemanticRun12FixtureTest()
      : SimTest(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void TearDown() override {
    child_ax_.reset();
    main_ax_.reset();
    SimTest::TearDown();
  }

  // Each width gets a fresh test/page. Navigation lifetime is tested separately.
  void LoadExactAction(int width) {
    ResizeView(gfx::Size(width, 600));
    // SimRequest is an in-process loader registration, not a socket/server.
    // Both resources must be registered before the parent parses the iframe.
    SimRequest main_resource("https://lunar-policy.test/action", "text/html");
    SimRequest child_resource("https://lunar-policy.test/nested-frame", "text/html");
    LoadURL("https://lunar-policy.test/action");
    main_resource.Complete(String::FromUtf8(kRun12Action));
    child_resource.Complete(String::FromUtf8(kRun12NestedFrame));

    auto* iframe = DynamicTo<HTMLIFrameElement>(GetDocument().QuerySelector(
        AtomicString(".fixture-case[data-case='nested-frame'] > iframe")));
    ASSERT_NE(nullptr, iframe);
    ASSERT_NE(nullptr, iframe->contentDocument());
    ASSERT_NE(&GetDocument(), iframe->contentDocument());
    EXPECT_EQ("nested-frame-canary", iframe->contentDocument()->body()
                  ->firstElementChild()->firstChild()->nodeValue());
    // The above string assertion is SETUP only, outside a service read scope.
    main_ax_ = std::make_unique<AXContext>(GetDocument(), ui::kAXModeDefaultForTests);
    child_ax_ = std::make_unique<AXContext>(*iframe->contentDocument(),
                                          ui::kAXModeDefaultForTests);
    // Bind exact DOM nodes and register requests before the ambient AX driver.
    // No fixture style normalization or OffsetMapping creation is performed.
  }

  Element* Case(const char* name) {
    return GetDocument().QuerySelector(AtomicString(
        String(".fixture-case[data-case='") + name + "']"));
  }

  Text* Anchor(const char* name) {
    Element* group = Case(name);
    if (!group || !group->lastElementChild()) return nullptr;
    return DynamicTo<Text>(group->lastElementChild()->firstChild());
  }

  using Disposition = SemanticDispositionV1;
  using Terminal = SemanticRequestTerminalV1;
  struct Slot { std::vector<SemanticRequestResultV1> results; };
  struct PositiveSource {
    WeakPersistent<Node> node;
    String expected;
  };
  struct Pending {
    std::shared_ptr<Slot> slot;
    std::unique_ptr<SelectedSemanticRequestV1> request;
  };
  struct StructuralProbe {
    bool ran = false;
    bool had_ax = false;
    bool cached_hidden = false;
    bool source_parent_style_missing = false;
    Disposition disposition = Disposition::kNotReady;
    SemanticBudgetV1 budget;
    SemanticAuditV1 audit;
  };
  struct NegativeSpec {
    const char* name;
    bool use_first_child_text;
    bool allow_actual_ax_absence;
    Disposition node_if_present;
    Disposition structure;
    unsigned content_reads;
  };
  static constexpr std::array<NegativeSpec, 12> kNegativeSpecs = {{
      {"display-none", true, true, Disposition::kForbiddenAncestor,
       Disposition::kForbiddenAncestor, 0},
      {"aria-hidden", true, true, Disposition::kForbiddenAncestor,
       Disposition::kForbiddenAncestor, 0},
      {"disabled-control", false, false, Disposition::kUnsupportedText,
       Disposition::kForbiddenAncestor, 0},
      {"protected", false, false, Disposition::kUnsupportedText,
       Disposition::kForbiddenAncestor, 0},
      {"ordinary-input", false, false, Disposition::kUnsupportedText,
       Disposition::kForbiddenAncestor, 0},
      {"password-input", false, false, Disposition::kUnsupportedText,
       Disposition::kForbiddenAncestor, 0},
      {"contenteditable-heading", true, true, Disposition::kForbiddenAncestor,
       Disposition::kForbiddenAncestor, 0},
      {"contenteditable-group", true, true, Disposition::kForbiddenAncestor,
       Disposition::kForbiddenAncestor, 0},
      {"nested-frame", false, false, Disposition::kUnsupportedText,
       Disposition::kForbiddenAncestor, 0},
      {"content-named-button", false, false, Disposition::kUnsupportedText,
       Disposition::kAdmitted, 0},
      {"empty-explicit-label", false, false, Disposition::kUnsupportedText,
       Disposition::kAdmitted, 1},
      {"unsupported-selector", false, false, Disposition::kUnsupportedText,
       Disposition::kUnsupportedText, 0},
  }};

  AXObjectCacheImpl& CacheFor(Document& document) {
    auto* cache = To<AXObjectCacheImpl>(document.ExistingAXObjectCache());
    CHECK(cache);
    return *cache;
  }

  Pending QueueRequest(Node& source, AXObjectCacheImpl& cache) {
    Pending pending;
    pending.slot = std::make_shared<Slot>();
    pending.request = SelectedSemanticRequestV1::Create(
        source.GetDocument(), cache, source, 12,
        base::TimeTicks::Now() + base::Seconds(5),
        task_environment().GetMainThreadTaskRunner(),
        blink::BindOnce(
            [](WeakPersistent<AXObjectCacheImpl> weak_cache,
               std::shared_ptr<Slot> slot, SemanticRequestResultV1 result) {
              EXPECT_FALSE(weak_cache && weak_cache->IsFrozen());
              slot->results.push_back(std::move(result));
            }, WeakPersistent<AXObjectCacheImpl>(&cache), pending.slot));
    EXPECT_TRUE(pending.slot->results.empty());
    return pending;
  }

  Pending QueueDocument(AXObjectCacheImpl& cache) {
    Pending pending;
    pending.slot = std::make_shared<Slot>();
    pending.request = SelectedSemanticRequestV1::CreateDocument(
        cache.GetDocument(), cache, 12,
        base::TimeTicks::Now() + base::Seconds(5),
        task_environment().GetMainThreadTaskRunner(),
        blink::BindOnce(
            [](WeakPersistent<AXObjectCacheImpl> weak_cache,
               std::shared_ptr<Slot> slot, SemanticRequestResultV1 result) {
              EXPECT_FALSE(weak_cache && weak_cache->IsFrozen());
              slot->results.push_back(std::move(result));
            }, WeakPersistent<AXObjectCacheImpl>(&cache), pending.slot));
    EXPECT_TRUE(pending.slot->results.empty());
    return pending;
  }

  std::shared_ptr<StructuralProbe> QueueStructure(Node& source,
                                                 AXObjectCacheImpl& cache) {
    auto probe = std::make_shared<StructuralProbe>();
    cache.ScheduleAXUpdateWithCallback(blink::BindOnce(
        [](WeakPersistent<AXObjectCacheImpl> cache, WeakPersistent<Node> source,
           std::shared_ptr<StructuralProbe> probe) {
          ASSERT_TRUE(cache && source);
          ASSERT_TRUE(cache->IsFrozen());
          ScriptForbiddenScope forbid_script;
          const AXObject* object = cache->Get(source.Get());
          probe->had_ax = object != nullptr;
          if (const auto* parent = DynamicTo<Element>(source->parentNode()))
            probe->source_parent_style_missing = !parent->GetComputedStyle();
          if (object) {
            ASSERT_FALSE(object->NeedsToUpdateCachedValues());
            probe->cached_hidden = object->IsHiddenViaStyle();
          }
          probe->disposition = ClassifySelectedStructureV1(
              *source, *cache, probe->budget, probe->audit);
          probe->ran = true;
        }, WeakPersistent<AXObjectCacheImpl>(&cache),
        WeakPersistent<Node>(&source), probe));
    return probe;
  }

  // Explicit ambient driver only. The request itself merely schedules; neither
  // request capture nor structural probe forces AX/layout/provenance creation.
  // These compatibility tests exercise the genuine frozen ready-callback hook,
  // not proof of ordinary unforced renderer scheduling or compositor coherence.
  void DriveAX(AXObjectCacheImpl& cache) {
    cache.MarkDocumentDirty();
    cache.UpdateAXForAllDocuments();
  }

  void BindMainPositives(const String& document_marker,
                         std::vector<PositiveSource>& sources) {
    Element* main = GetDocument().QuerySelector(AtomicString("main"));
    ASSERT_NE(nullptr, main);
    Element* document_p = main->firstElementChild();
    ASSERT_NE(nullptr, document_p);
    Element* canary_p = document_p->nextElementSibling();
    ASSERT_NE(nullptr, canary_p);
    auto* document_text = DynamicTo<Text>(document_p->firstChild());
    auto* canary_text = DynamicTo<Text>(canary_p->firstChild());
    Element* button = GetDocument().getElementById(AtomicString("allowed-action"));
    Element* status = GetDocument().getElementById(AtomicString("action-status"));
    ASSERT_TRUE(document_text && canary_text && button && status);
    auto* status_text = DynamicTo<Text>(status->firstChild());
    ASSERT_NE(nullptr, status_text);
    // The exact UA button is empty: do not substitute visible button content.
    ASSERT_EQ(nullptr, button->firstChild());
    sources.push_back({WeakPersistent<Node>(document_text), document_marker});
    sources.push_back({WeakPersistent<Node>(canary_text),
                      "lunar-feasibility-visible-canary:run-12"});
    sources.push_back({WeakPersistent<Node>(button),
                      "Activate controlled fixture action"});
    sources.push_back({WeakPersistent<Node>(status_text),
                      "lunar-action:pending:run-12"});
  }

  void ExpectPositive(const Pending& pending, const String& expected) {
    ASSERT_EQ(1u, pending.slot->results.size());
    const auto& result = pending.slot->results.front();
    EXPECT_EQ(Terminal::kPolicyResult, result.terminal);
    EXPECT_EQ(12u, result.epoch);
    EXPECT_EQ(Disposition::kAdmitted, result.node.disposition);
    EXPECT_EQ(expected, result.node.text);
    EXPECT_EQ(1u, result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
    EXPECT_LE(result.audit.structural_reads, 4096u);
    EXPECT_LE(result.budget.nodes, 256u);
    EXPECT_LE(result.budget.fragments, 512u);
    EXPECT_LE(result.budget.relation_steps, 4096u);
    EXPECT_LE(result.budget.utf8_bytes, 65536u);
  }

  void ExpectNegative(const Pending& pending, Disposition disposition,
                      unsigned permitted_content_reads = 0) {
    ASSERT_EQ(1u, pending.slot->results.size());
    const auto& result = pending.slot->results.front();
    EXPECT_EQ(Terminal::kPolicyResult, result.terminal);
    EXPECT_EQ(12u, result.epoch);
    EXPECT_EQ(disposition, result.node.disposition);
    EXPECT_TRUE(result.node.text.empty());
    EXPECT_EQ(permitted_content_reads, result.audit.text_reads);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
  }

  void CheckExactAction() {
    auto& cache = CacheFor(GetDocument());
    std::vector<PositiveSource> sources;
    ASSERT_NO_FATAL_FAILURE(BindMainPositives(
        "lunar-document:safe:run-12:revision-1", sources));
    for (const char* name : kRun12Cases) {
      auto* anchor = Anchor(name);
      ASSERT_NE(nullptr, anchor);
      sources.push_back({WeakPersistent<Node>(anchor),
          String("lunar-feasibility-anchor:") + name + ":run-12"});
    }
    ASSERT_EQ(16u, sources.size());
    std::vector<Pending> positives;
    for (const auto& source : sources)
      positives.push_back(QueueRequest(*source.node, cache));

    std::vector<Pending> negatives;
    std::vector<std::shared_ptr<StructuralProbe>> probes;
    for (const auto& spec : kNegativeSpecs) {
      SCOPED_TRACE(spec.name);
      Element* group = Case(spec.name);
      ASSERT_NE(nullptr, group);
      Element* first = group->firstElementChild();
      ASSERT_NE(nullptr, first);
      Node* source = spec.use_first_child_text
          ? static_cast<Node*>(DynamicTo<Text>(first->firstChild())) : first;
      ASSERT_NE(nullptr, source);
      // This separate actual-ready probe distinguishes an existing rejected AX
      // source from true AX absence without inventing objects or hidden flags.
      probes.push_back(QueueStructure(*source, cache));
      negatives.push_back(QueueRequest(*source, cache));
    }
    ASSERT_EQ(12u, negatives.size());

    // Additional content-named child check: the element's missing direct label
    // is distinct from exclusion of its actual Text descendant.
    auto* named_button_text = DynamicTo<Text>(
        Case("content-named-button")->firstElementChild()->firstChild());
    ASSERT_NE(nullptr, named_button_text);
    auto named_text_probe = QueueStructure(*named_button_text, cache);
    auto named_text = QueueRequest(*named_button_text, cache);

    DriveAX(cache);
    for (const auto& pending : positives) EXPECT_TRUE(pending.slot->results.empty());
    for (const auto& pending : negatives) EXPECT_TRUE(pending.slot->results.empty());
    EXPECT_TRUE(named_text.slot->results.empty());
    task_environment().RunUntilIdle();
    for (size_t i = 0; i < positives.size(); ++i) {
      SCOPED_TRACE(sources[i].expected.Utf8());  // Expected test literal only.
      ASSERT_NO_FATAL_FAILURE(ExpectPositive(positives[i], sources[i].expected));
    }
    for (size_t i = 0; i < negatives.size(); ++i) {
      const auto& spec = kNegativeSpecs[i];
      const auto& probe = *probes[i];
      SCOPED_TRACE(spec.name);
      ASSERT_TRUE(probe.ran);
      if (String(spec.name) == "display-none" && !probe.had_ax &&
          probe.disposition == Disposition::kNotReady) {
        // A truly absent hidden source can lack even cached parent style.
        // Record this missing-state fence, not an invented hidden-AX rejection.
        EXPECT_TRUE(probe.source_parent_style_missing);
      } else {
        EXPECT_EQ(spec.structure, probe.disposition);
      }
      EXPECT_EQ(0u, probe.audit.text_reads);
      EXPECT_EQ(0u, probe.audit.forbidden_reads);
      if (!spec.allow_actual_ax_absence) ASSERT_TRUE(probe.had_ax);
      if (String(spec.name) == "display-none" && probe.had_ax)
        EXPECT_TRUE(probe.cached_hidden);
      // Real AX absence is NOT evidence that the node reader classified hidden
      // AX. Its request outcome is exactly StaleDocument, and the separate
      // structural result above remains independently required.
      ASSERT_NO_FATAL_FAILURE(ExpectNegative(
          negatives[i], probe.had_ax ? spec.node_if_present
                                    : Disposition::kStaleDocument,
          probe.had_ax ? spec.content_reads : 0));
    }
    ASSERT_TRUE(named_text_probe->ran);
    EXPECT_EQ(Disposition::kForbiddenAncestor, named_text_probe->disposition);
    EXPECT_EQ(0u, named_text_probe->audit.text_reads);
    EXPECT_EQ(0u, named_text_probe->audit.forbidden_reads);
    ASSERT_NO_FATAL_FAILURE(ExpectNegative(named_text,
        named_text_probe->had_ax ? Disposition::kForbiddenAncestor
                                : Disposition::kStaleDocument));

    // Child Text is bound to the REAL child Document/cache, never treated as
    // main-document content or looked up through the main cache.
    auto* iframe = DynamicTo<HTMLIFrameElement>(
        Case("nested-frame")->firstElementChild());
    ASSERT_TRUE(iframe && iframe->contentDocument());
    Document& child_document = *iframe->contentDocument();
    auto* child_text = DynamicTo<Text>(
        child_document.body()->firstElementChild()->firstChild());
    ASSERT_NE(nullptr, child_text);
    auto& child_cache = CacheFor(child_document);
    auto child_probe = QueueStructure(*child_text, child_cache);
    auto child_read = QueueRequest(*child_text, child_cache);
    DriveAX(child_cache);
    EXPECT_TRUE(child_read.slot->results.empty());
    task_environment().RunUntilIdle();
    ASSERT_TRUE(child_probe->ran);
    EXPECT_EQ(Disposition::kForbiddenAncestor, child_probe->disposition);
    EXPECT_EQ(0u, child_probe->audit.text_reads);
    EXPECT_EQ(0u, child_probe->audit.forbidden_reads);
    ASSERT_NO_FATAL_FAILURE(ExpectNegative(child_read,
        child_probe->had_ax ? Disposition::kForbiddenAncestor
                           : Disposition::kStaleDocument));
    // Further task execution must not add a completion to any captured slot.
    task_environment().FastForwardBy(base::Seconds(6));
    for (const auto& pending : positives) EXPECT_EQ(1u, pending.slot->results.size());
    for (const auto& pending : negatives) EXPECT_EQ(1u, pending.slot->results.size());
    EXPECT_EQ(1u, named_text.slot->results.size());
    EXPECT_EQ(1u, child_read.slot->results.size());
  }

  void CheckExactWholeAction() {
    std::vector<PositiveSource> sources;
    ASSERT_NO_FATAL_FAILURE(BindMainPositives(
        "lunar-document:safe:run-12:revision-1", sources));
    for (const char* name : kRun12Cases) {
      auto* anchor = Anchor(name);
      ASSERT_NE(nullptr, anchor);
      sources.push_back({WeakPersistent<Node>(anchor),
          String("lunar-feasibility-anchor:") + name + ":run-12"});
    }
    ASSERT_EQ(16u, sources.size());
    std::multiset<std::string> expected;
    for (const auto& source : sources)
      expected.insert(source.expected.Utf8().data());
    auto& cache = CacheFor(GetDocument());
    auto pending = QueueDocument(cache);
    DriveAX(cache);
    EXPECT_TRUE(pending.slot->results.empty());
    task_environment().RunUntilIdle();
    ASSERT_EQ(1u, pending.slot->results.size());
    const auto& result = pending.slot->results.front();
    EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
    EXPECT_EQ(SemanticRequestKindV1::kDocument, result.kind);
    EXPECT_EQ(12u, result.epoch);
    EXPECT_EQ(SemanticDispositionV1::kAdmitted,
              result.observation.disposition);
    EXPECT_TRUE(result.node.text.empty());
    EXPECT_EQ(0u, result.audit.forbidden_reads);
    EXPECT_EQ(17u, result.audit.text_reads);
    EXPECT_LE(result.budget.nodes, 256u);
    EXPECT_LE(result.budget.fragments, 512u);
    EXPECT_LE(result.budget.relation_steps, 4096u);
    ASSERT_EQ(16u, result.observation.entries.size());
    std::multiset<std::string> actual;
    unsigned buttons = 0;
    for (const auto& entry : result.observation.entries) {
      actual.insert(entry.text.Utf8().data());
      buttons += entry.role == SemanticObservationRoleV1::kButton;
    }
    EXPECT_EQ(expected, actual);
    EXPECT_EQ(1u, buttons);
    // The literal fixture places the four main entries first, followed by
    // the twelve structural-case anchors in kRun12Cases DOM order.
    for (unsigned i = 0; i < sources.size(); ++i) {
      SCOPED_TRACE(i);
      EXPECT_EQ(sources[i].expected, result.observation.entries[i].text);
      EXPECT_EQ(i == 2 ? SemanticObservationRoleV1::kButton
                       : SemanticObservationRoleV1::kText,
                result.observation.entries[i].role);
    }
  }

  void LoadExactSimple(const char* url, const char* literal) {
    ResizeView(gfx::Size(800, 600));
    SimRequest resource(url, "text/html");
    LoadURL(url);
    resource.Complete(String::FromUtf8(literal));
    main_ax_ = std::make_unique<AXContext>(GetDocument(), ui::kAXModeDefaultForTests);
  }

  void CheckExactSimple(const String& document_marker) {
    std::vector<PositiveSource> sources;
    ASSERT_NO_FATAL_FAILURE(BindMainPositives(document_marker, sources));
    ASSERT_EQ(4u, sources.size());
    auto& cache = CacheFor(GetDocument());
    std::vector<Pending> pending;
    for (const auto& source : sources)
      pending.push_back(QueueRequest(*source.node, cache));
    DriveAX(cache);
    for (const auto& item : pending) EXPECT_TRUE(item.slot->results.empty());
    task_environment().RunUntilIdle();
    for (size_t i = 0; i < pending.size(); ++i)
      ASSERT_NO_FATAL_FAILURE(ExpectPositive(pending[i], sources[i].expected));
  }

  void CheckExactWholeSimple(const String& document_marker) {
    std::vector<PositiveSource> sources;
    ASSERT_NO_FATAL_FAILURE(BindMainPositives(document_marker, sources));
    ASSERT_EQ(4u, sources.size());
    std::multiset<std::string> expected;
    for (const auto& source : sources)
      expected.insert(source.expected.Utf8().data());
    auto& cache = CacheFor(GetDocument());
    auto pending = QueueDocument(cache);
    DriveAX(cache);
    EXPECT_TRUE(pending.slot->results.empty());
    task_environment().RunUntilIdle();
    ASSERT_EQ(1u, pending.slot->results.size());
    const auto& result = pending.slot->results.front();
    EXPECT_EQ(SemanticRequestKindV1::kDocument, result.kind);
    EXPECT_EQ(SemanticRequestTerminalV1::kPolicyResult, result.terminal);
    EXPECT_EQ(SemanticDispositionV1::kAdmitted,
              result.observation.disposition);
    EXPECT_EQ(0u, result.audit.forbidden_reads);
    ASSERT_EQ(4u, result.observation.entries.size());
    std::multiset<std::string> actual;
    for (const auto& entry : result.observation.entries)
      actual.insert(entry.text.Utf8().data());
    EXPECT_EQ(expected, actual);
    for (unsigned i = 0; i < sources.size(); ++i) {
      SCOPED_TRACE(i);
      EXPECT_EQ(sources[i].expected, result.observation.entries[i].text);
      EXPECT_EQ(i == 2 ? SemanticObservationRoleV1::kButton
                       : SemanticObservationRoleV1::kText,
                result.observation.entries[i].role);
    }
  }

  // Called only after ordinary lifecycle/real readiness. Uses actual fragment
  // coordinates, not text strings, forced layout, OffsetMapping, or fake lines.
  void ExpectActualWrappedAnchor() {
    bool any_distinct_line_top = false;
    for (const char* name : kRun12Cases) {
      Text* node = Anchor(name);
      ASSERT_NE(nullptr, node);
      const auto* text = DynamicTo<LayoutText>(node->GetLayoutObject());
      ASSERT_NE(nullptr, text);
      // Exact fixture has Text directly under its p grid child.
      const auto* box = DynamicTo<LayoutBlockFlow>(text->Parent());
      ASSERT_NE(nullptr, box);
      ASSERT_EQ(1u, box->PhysicalFragmentCount());
      const auto* fragment = box->GetPhysicalFragment(0);
      ASSERT_NE(nullptr, fragment);
      const auto* items = fragment->Items();
      ASSERT_NE(nullptr, items);
      ASSERT_EQ(0u, items->SizeOfEarlierFragments());
      const auto span = items->Items();
      const auto first = text->FirstInlineFragmentItemIndex();
      ASSERT_GT(first, 0u);
      ASSERT_LE(first, span.size());
      size_t index = first - 1;
      std::optional<LayoutUnit> first_top;
      unsigned count = 0;
      for (;;) {
        ASSERT_LT(count++, 512u);
        const auto& item = span[index];
        ASSERT_EQ(text, item.GetLayoutObject());
        ASSERT_EQ(FragmentItem::kText, item.Type());
        const auto& rect = item.RectInContainerFragment();
        ASSERT_FALSE(rect.IsEmpty());
        if (!first_top) first_top = rect.Y();
        else any_distinct_line_top |= rect.Y() != *first_top;
        const auto delta = item.DeltaToNextForSameLayoutObject();
        if (!delta) break;
        ASSERT_LT(delta, span.size() - index);
        index += delta;
      }
    }
    EXPECT_TRUE(any_distinct_line_top)
        << "360px fixture must actually wrap; do not change its CSS/font to pass";
  }

  std::unique_ptr<AXContext> main_ax_;
  std::unique_ptr<AXContext> child_ax_;
};

TEST_F(SelectedSemanticRun12FixtureTest, ExactRun12DesktopCapturesAllCases) {
  ASSERT_NO_FATAL_FAILURE(LoadExactAction(800));
  ASSERT_NO_FATAL_FAILURE(CheckExactAction());
}

TEST_F(SelectedSemanticRun12FixtureTest, WholeDocumentDesktopCapturesSixteen) {
  ASSERT_NO_FATAL_FAILURE(LoadExactAction(800));
  ASSERT_NO_FATAL_FAILURE(CheckExactWholeAction());
}

TEST_F(SelectedSemanticRun12FixtureTest, WholeDocumentNarrowCapturesSixteen) {
  ASSERT_NO_FATAL_FAILURE(LoadExactAction(360));
  ASSERT_NO_FATAL_FAILURE(CheckExactWholeAction());
  ASSERT_NO_FATAL_FAILURE(ExpectActualWrappedAnchor());
}

TEST_F(SelectedSemanticRun12FixtureTest, ExactRun12NarrowCapturesAllCasesAndWraps) {
  ASSERT_NO_FATAL_FAILURE(LoadExactAction(360));
  ASSERT_NO_FATAL_FAILURE(CheckExactAction());
  ASSERT_NO_FATAL_FAILURE(ExpectActualWrappedAnchor());
}

TEST_F(SelectedSemanticRun12FixtureTest, ExactRun12NoopDocumentCompatibility) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/action-noop", kRun12ActionNoop));
  ASSERT_NO_FATAL_FAILURE(CheckExactSimple("lunar-document:noop:run-12:revision-1"));
}

TEST_F(SelectedSemanticRun12FixtureTest, WholeDocumentNoopCapturesFour) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/action-noop", kRun12ActionNoop));
  ASSERT_NO_FATAL_FAILURE(CheckExactWholeSimple(
      "lunar-document:noop:run-12:revision-1"));
}

TEST_F(SelectedSemanticRun12FixtureTest, ExactRun12ReloadOldDocumentCompatibility) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/reload-old", kRun12ReloadOld));
  ASSERT_NO_FATAL_FAILURE(CheckExactSimple("lunar-document:reload-old:run-12:revision-1"));
}

TEST_F(SelectedSemanticRun12FixtureTest, WholeDocumentReloadOldCapturesFour) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/reload-old", kRun12ReloadOld));
  ASSERT_NO_FATAL_FAILURE(CheckExactWholeSimple(
      "lunar-document:reload-old:run-12:revision-1"));
}

TEST_F(SelectedSemanticRun12FixtureTest, ExactRun12ReloadNewDocumentCompatibility) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/reload-new", kRun12ReloadNew));
  ASSERT_NO_FATAL_FAILURE(CheckExactSimple("lunar-document:reload-new:run-12:revision-2"));
}

TEST_F(SelectedSemanticRun12FixtureTest, WholeDocumentReloadNewCapturesFour) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/reload-new", kRun12ReloadNew));
  ASSERT_NO_FATAL_FAILURE(CheckExactWholeSimple(
      "lunar-document:reload-new:run-12:revision-2"));
}

TEST_F(SelectedSemanticRun12FixtureTest,
       LateHandlerAfterRealDocumentReplacementCannotRebind) {
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/reload-old", kRun12ReloadOld));
  auto& old_cache = CacheFor(GetDocument());
  const auto old_token = GetDocument().Token();
  WeakPersistent<Document> old_document(&GetDocument());
  auto* old_target = DynamicTo<Text>(GetDocument()
      .getElementById(AtomicString("action-status"))->firstChild());
  ASSERT_NE(nullptr, old_target);
  auto pending = QueueRequest(*old_target, old_cache);
  auto pending_document = QueueDocument(old_cache);
  ASSERT_TRUE(SelectedSemanticRequestTestPeer::Pending(*pending_document.request));

  // Real simulated navigation and document replacement, not a manufactured
  // token/frame pointer. Do not drive the old AX callback before navigation.
  SimRequest replacement("https://lunar-policy.test/reload-new", "text/html");
  LoadURL("https://lunar-policy.test/reload-new");
  replacement.Complete(String::FromUtf8(kRun12ReloadNew));
  ASSERT_NE(old_document.Get(), &GetDocument());
  EXPECT_NE(old_token, GetDocument().Token());
  if (old_document) EXPECT_FALSE(old_document->IsActive());
  ASSERT_NE(nullptr, GetDocument().getElementById(AtomicString("action-status")));
  EXPECT_TRUE(pending.slot->results.empty());
  EXPECT_TRUE(pending_document.slot->results.empty());

  // The old cache can discard queued closures when disposed. Exercise the
  // delayed-handler fence explicitly after REAL replacement; this is not a
  // claim that Chromium naturally invokes a disposed cache's callback.
  SelectedSemanticRequestTestPeer::Ready(*pending.request);
  SelectedSemanticRequestTestPeer::Ready(*pending_document.request);
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, pending.slot->results.size());
  const auto& stale = pending.slot->results.front();
  EXPECT_EQ(Terminal::kStaleContext, stale.terminal);
  EXPECT_TRUE(stale.node.text.empty());
  EXPECT_EQ(0u, stale.audit.text_reads);
  EXPECT_EQ(0u, stale.audit.structural_reads);
  EXPECT_EQ(0u, stale.audit.forbidden_reads);
  ASSERT_EQ(1u, pending_document.slot->results.size());
  const auto& stale_document = pending_document.slot->results.front();
  EXPECT_EQ(SemanticRequestKindV1::kDocument, stale_document.kind);
  EXPECT_EQ(Terminal::kStaleContext, stale_document.terminal);
  EXPECT_TRUE(stale_document.observation.entries.empty());
  EXPECT_EQ(0u, stale_document.audit.text_reads);

  // A separately admitted new request can read the exact replacement document.
  // It never substitutes for the old request, whose one result stays stale.
  main_ax_.reset();
  main_ax_ = std::make_unique<AXContext>(GetDocument(), ui::kAXModeDefaultForTests);
  ASSERT_NO_FATAL_FAILURE(CheckExactSimple("lunar-document:reload-new:run-12:revision-2"));
  pending.request.reset();
  pending_document.request.reset();
  task_environment().FastForwardBy(base::Seconds(6));
  EXPECT_EQ(1u, pending.slot->results.size());
  EXPECT_EQ(1u, pending_document.slot->results.size());
}

TEST_F(SelectedSemanticRun12FixtureTest,
       RealResizeObserverDetachesPendingTargetBeforeNaturalAXReady) {
  // Deliberately separate from the immutable Run12 compatibility literals.
  // Loading/setup creates the real AX context but does not force an AX update.
  ASSERT_NO_FATAL_FAILURE(LoadExactSimple(
      "https://lunar-policy.test/resize-detach", R"HTML(<!doctype html>
<html><body><div id="resize-trigger" style="width:100px;height:20px"></div>
<p id="target">Allowed before removal</p></body></html>)HTML"));
  auto* target_element = GetDocument().getElementById(AtomicString("target"));
  auto* trigger = GetDocument().getElementById(AtomicString("resize-trigger"));
  ASSERT_TRUE(target_element && trigger);
  Persistent<Text> old_target(DynamicTo<Text>(target_element->firstChild()));
  ASSERT_TRUE(old_target);
  ASSERT_TRUE(old_target->isConnected());
  auto& cache = CacheFor(GetDocument());
  Persistent<SemanticTargetDetachingResizeDelegate> delegate(
      MakeGarbageCollected<SemanticTargetDetachingResizeDelegate>(
          old_target.Get(), &cache));
  Persistent<ResizeObserver> observer(ResizeObserver::Create(&Window(), delegate.Get()));
  observer->observe(trigger);
  auto pending = QueueRequest(*old_target, cache);
  EXPECT_EQ(0u, delegate->Calls());

  // Real style mutation requests a normal lifecycle frame. The observer removes
  // the exact source Text during that frame; it never calls policy or AX update.
  trigger->setAttribute(html_names::kStyleAttr,
                        AtomicString("width:200px;height:20px"));
  ASSERT_FALSE(Compositor().DeferMainFrameUpdate());
  ASSERT_TRUE(Compositor().NeedsBeginFrame());
  // Advance mock time without running any tasks. This puts the ordinary 16ms
  // BeginFrame timestamp in the past, avoiding SimCompositor's real sleep.
  task_environment().AdvanceClock(base::Milliseconds(17));
  Compositor().BeginFrame();
  ASSERT_GT(delegate->Calls(), 0u);
  ASSERT_TRUE(delegate->Removed());
  EXPECT_FALSE(old_target->isConnected());
  EXPECT_EQ(nullptr, target_element->firstChild());
  EXPECT_TRUE(pending.slot->results.empty());
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, pending.slot->results.size());
  const auto& result = pending.slot->results.front();
  EXPECT_EQ(Terminal::kStaleContext, result.terminal);
  EXPECT_EQ(12u, result.epoch);
  EXPECT_TRUE(result.node.text.empty());
  EXPECT_EQ(0u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.structural_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  observer->disconnect();
  pending.request.reset();
  task_environment().FastForwardBy(base::Seconds(6));
  EXPECT_EQ(1u, pending.slot->results.size());
  // No DriveAX, UpdateAXForAllDocuments or test-peer Ready call occurs here.
  // If natural delivery is unavailable, fail this test; do not force success.
}

TEST_F(SelectedSemanticRun12FixtureTest,
       LateHandlerAfterRealIFrameDetachCannotReadOldDocument) {
  // Another minimal test-owned document: no Run12 literal or mandatory layout
  // is changed to arrange the detachment.
  ResizeView(gfx::Size(800, 600));
  SimRequest main_resource("https://lunar-policy.test/detach-parent", "text/html");
  SimRequest child_resource("https://lunar-policy.test/detach-child", "text/html");
  LoadURL("https://lunar-policy.test/detach-parent");
  main_resource.Complete(R"HTML(<!doctype html><html><body>
<iframe id="frame" src="/detach-child" style="width:200px;height:100px"></iframe>
</body></html>)HTML");
  child_resource.Complete(R"HTML(<!doctype html><html><body>
<p id="target">Child text must stay unread</p></body></html>)HTML");
  Persistent<HTMLIFrameElement> iframe(DynamicTo<HTMLIFrameElement>(
      GetDocument().getElementById(AtomicString("frame"))));
  ASSERT_TRUE(iframe && iframe->contentDocument());
  Persistent<Document> child_document(iframe->contentDocument());
  Persistent<LocalFrame> child_frame(child_document->GetFrame());
  ASSERT_TRUE(child_frame && child_frame->IsAttached());
  auto* child_target = child_document->getElementById(AtomicString("target"));
  ASSERT_NE(nullptr, child_target);
  Persistent<Text> child_text(DynamicTo<Text>(child_target->firstChild()));
  ASSERT_TRUE(child_text);
  main_ax_ = std::make_unique<AXContext>(GetDocument(), ui::kAXModeDefaultForTests);
  child_ax_ = std::make_unique<AXContext>(*child_document, ui::kAXModeDefaultForTests);
  auto& child_cache = CacheFor(*child_document);
  auto pending = QueueRequest(*child_text, child_cache);
  auto pending_document = QueueDocument(child_cache);
  ASSERT_TRUE(SelectedSemanticRequestTestPeer::Pending(*pending_document.request));

  // Real DOM removal calls HTMLFrameOwnerElement::DisconnectContentFrame ->
  // Frame::Detach(kRemove). Keeping old objects alive tests stale ownership,
  // not GC absence and not a fabricated IsAttached/frame state.
  iframe->remove();
  ASSERT_FALSE(child_frame->IsAttached());
  EXPECT_TRUE(child_frame->IsDetached());
  EXPECT_FALSE(child_document->IsActive());
  EXPECT_EQ(nullptr, iframe->ContentFrame());
  EXPECT_TRUE(pending.slot->results.empty());
  EXPECT_TRUE(pending_document.slot->results.empty());
  // As with navigation, disposal may discard the original queued closure.
  // Explicit late-handler delivery tests its fence after real frame teardown;
  // it does not claim the disposed cache naturally emits an AX-ready callback.
  SelectedSemanticRequestTestPeer::Ready(*pending.request);
  SelectedSemanticRequestTestPeer::Ready(*pending_document.request);
  task_environment().RunUntilIdle();
  ASSERT_EQ(1u, pending.slot->results.size());
  const auto& result = pending.slot->results.front();
  EXPECT_EQ(Terminal::kStaleContext, result.terminal);
  EXPECT_TRUE(result.node.text.empty());
  EXPECT_EQ(0u, result.audit.text_reads);
  EXPECT_EQ(0u, result.audit.structural_reads);
  EXPECT_EQ(0u, result.audit.forbidden_reads);
  ASSERT_EQ(1u, pending_document.slot->results.size());
  const auto& document_result = pending_document.slot->results.front();
  EXPECT_EQ(SemanticRequestKindV1::kDocument, document_result.kind);
  EXPECT_EQ(Terminal::kStaleContext, document_result.terminal);
  EXPECT_TRUE(document_result.observation.entries.empty());
  EXPECT_EQ(0u, document_result.audit.text_reads);
  pending.request.reset();
  pending_document.request.reset();
  task_environment().FastForwardBy(base::Seconds(6));
  EXPECT_EQ(1u, pending.slot->results.size());
  EXPECT_EQ(1u, pending_document.slot->results.size());
}

}  // namespace

}  // namespace blink
