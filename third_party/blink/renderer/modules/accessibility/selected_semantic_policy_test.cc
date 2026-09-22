// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/accessibility/selected_semantic_policy.h"

#include <memory>
#include <optional>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/core/accessibility/ax_context.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/frame_test_helpers.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/testing/scoped_mock_overlay_scrollbars.h"
#include "third_party/blink/renderer/core/view_transition/dom_view_transition.h"
#include "third_party/blink/renderer/core/view_transition/scoped_view_transition.h"
#include "third_party/blink/renderer/core/view_transition/view_transition_supplement.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object_cache_impl.h"
#include "third_party/blink/renderer/modules/accessibility/testing/accessibility_test.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/testing/task_environment.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

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

class SelectedSemanticPolicyTest : public AccessibilityTest {
 protected:
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

}  // namespace blink
