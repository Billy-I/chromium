// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/selected_semantic_policy.h"

#include "third_party/blink/renderer/core/layout/layout_object_inlines.h"
#include "third_party/blink/renderer/core/layout/layout_invalidation_reason.h"
#include "third_party/blink/renderer/platform/graphics/subtree_paint_property_update_reason.h"

#include <memory>
#include <limits>
#include <optional>

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
#include "third_party/blink/renderer/core/dom/selected_semantic_read_scope.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/frame_test_helpers.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/inline/inline_node_data.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/physical_box_fragment.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/page.h"
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
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

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

}  // namespace blink
