// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/web/web_selected_semantic_session.h"

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/forms/html_button_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object.h"
#include "third_party/blink/renderer/modules/accessibility/ax_object_cache_impl.h"
#include "third_party/blink/renderer/modules/accessibility/selected_semantic_policy.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {
namespace {

WebSelectedSemanticTerminalV1 ToWebTerminal(SemanticRequestTerminalV1 value) {
  switch (value) {
    case SemanticRequestTerminalV1::kPolicyResult:
      return WebSelectedSemanticTerminalV1::kPolicyResult;
    case SemanticRequestTerminalV1::kCancelled:
      return WebSelectedSemanticTerminalV1::kCancelled;
    case SemanticRequestTerminalV1::kDeadlineExceeded:
      return WebSelectedSemanticTerminalV1::kDeadlineExceeded;
    case SemanticRequestTerminalV1::kStaleContext:
      return WebSelectedSemanticTerminalV1::kStaleContext;
    case SemanticRequestTerminalV1::kInvalidRequest:
      return WebSelectedSemanticTerminalV1::kInvalidRequest;
  }
  return WebSelectedSemanticTerminalV1::kInvalidRequest;
}

WebSelectedSemanticDispositionV1 ToWebDisposition(SemanticDispositionV1 value) {
  switch (value) {
    case SemanticDispositionV1::kAdmitted:
      return WebSelectedSemanticDispositionV1::kAdmitted;
    case SemanticDispositionV1::kForbiddenAncestor:
      return WebSelectedSemanticDispositionV1::kForbiddenAncestor;
    case SemanticDispositionV1::kOwnZeroSize:
      return WebSelectedSemanticDispositionV1::kOwnZeroSize;
    case SemanticDispositionV1::kOffscreen:
      return WebSelectedSemanticDispositionV1::kOffscreen;
    case SemanticDispositionV1::kUnsupportedGeometry:
      return WebSelectedSemanticDispositionV1::kUnsupportedGeometry;
    case SemanticDispositionV1::kUnsupportedText:
      return WebSelectedSemanticDispositionV1::kUnsupportedText;
    case SemanticDispositionV1::kStaleDocument:
      return WebSelectedSemanticDispositionV1::kStaleDocument;
    case SemanticDispositionV1::kNotReady:
      return WebSelectedSemanticDispositionV1::kNotReady;
    case SemanticDispositionV1::kLimitExceeded:
      return WebSelectedSemanticDispositionV1::kLimitExceeded;
  }
  return WebSelectedSemanticDispositionV1::kNotReady;
}

WebSelectedSemanticOperationStatusV1 ToOperationStatus(
    SemanticDispositionV1 value) {
  switch (value) {
    case SemanticDispositionV1::kAdmitted:
      return WebSelectedSemanticOperationStatusV1::kOk;
    case SemanticDispositionV1::kForbiddenAncestor:
    case SemanticDispositionV1::kOwnZeroSize:
    case SemanticDispositionV1::kOffscreen:
    case SemanticDispositionV1::kUnsupportedText:
      return WebSelectedSemanticOperationStatusV1::kPolicyExcluded;
    case SemanticDispositionV1::kUnsupportedGeometry:
      return WebSelectedSemanticOperationStatusV1::kUnsupportedGeometry;
    case SemanticDispositionV1::kStaleDocument:
      return WebSelectedSemanticOperationStatusV1::kStaleDocument;
    case SemanticDispositionV1::kNotReady:
      return WebSelectedSemanticOperationStatusV1::kNotReady;
    case SemanticDispositionV1::kLimitExceeded:
      return WebSelectedSemanticOperationStatusV1::kLimitExceeded;
  }
  return WebSelectedSemanticOperationStatusV1::kInvalidRequest;
}

void CloseWithoutPageData(WebSelectedSemanticCaptureV1& output,
                          WebSelectedSemanticTerminalV1 terminal) {
  output.terminal = terminal;
  output.disposition = WebSelectedSemanticDispositionV1::kNotReady;
  output.entries.clear();
  output.reserved_output_bytes = 0;
}

}  // namespace

class WebSelectedSemanticSession::Impl {
 public:
  Impl(WebDocument web_document,
       scoped_refptr<base::SingleThreadTaskRunner> runner)
      : runner_(std::move(runner)) {
    CHECK(IsMainThread() && runner_ && runner_->RunsTasksInCurrentSequence());
    if (web_document.IsNull())
      return;
    Document* document = web_document.Unwrap<Document>();
    if (!document || !document->IsActive())
      return;
    LocalFrame* frame = document->GetFrame();
    if (!frame || !frame->IsAttached() || !frame->IsMainFrame() ||
        frame->GetDocument() != document)
      return;
    document_ = document;
    frame_ = frame;
    document_token_ = document->Token();
    frame_token_ = frame->GetLocalFrameToken();
  }

  ~Impl() {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    if (request_)
      request_->Cancel();
    if (operation_request_)
      operation_request_->Cancel();
  }

  void Capture(uint64_t epoch, base::TimeTicks deadline, Completion callback) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence() && callback);
    if (operation_request_) {
      WebSelectedSemanticCaptureV1 output;
      CloseWithoutPageData(output,
                           WebSelectedSemanticTerminalV1::kInvalidRequest);
      runner_->PostTask(FROM_HERE,
                        base::BindOnce(std::move(callback), std::move(output)));
      return;
    }
    Revoke(WebSelectedSemanticTerminalV1::kStaleContext);
    auto state = std::make_shared<CaptureState>();
    state->deadline = deadline;
    active_ = state;
    if (!HasCurrentDocument()) {
      SemanticRequestResultV1 result;
      result.terminal = SemanticRequestTerminalV1::kStaleContext;
      PostResult(std::move(state), std::move(callback), std::move(result));
      return;
    }
    auto* cache = To<AXObjectCacheImpl>(document_->ExistingAXObjectCache());
    if (!cache) {
      SemanticRequestResultV1 result;
      result.terminal = SemanticRequestTerminalV1::kStaleContext;
      PostResult(std::move(state), std::move(callback), std::move(result));
      return;
    }
    state->device_scale = CurrentPureDeviceScale();
    capture_cache_ = cache;
    request_ = SelectedSemanticRequestV1::CreateDocument(
        *document_, *cache, epoch, deadline, runner_,
        base::BindOnce(&Impl::OnResult, weak_factory_.GetWeakPtr(),
                       std::move(state), std::move(callback)));
  }

  void Press(uint64_t slot,
             uint64_t epoch,
             base::TimeTicks deadline,
             PressCompletion callback) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence() && callback);
    auto fail = [&](WebSelectedSemanticOperationStatusV1 status) {
      WebSelectedSemanticPressV1 result;
      result.status = status;
      runner_->PostTask(FROM_HERE,
                        base::BindOnce(std::move(callback), std::move(result)));
    };
    if (operation_request_ || !slot || deadline.is_null() ||
        base::TimeTicks::Now() >= deadline) {
      fail(WebSelectedSemanticOperationStatusV1::kInvalidRequest);
      return;
    }
    SlotBinding* binding = FindSlot(slot);
    if (!binding || binding->used || !HasLiveButtonSlot(slot)) {
      fail(WebSelectedSemanticOperationStatusV1::kStaleDocument);
      return;
    }
    auto* cache = CurrentCache();
    Node* node = binding->node.Get();
    if (!cache || !node) {
      fail(WebSelectedSemanticOperationStatusV1::kStaleDocument);
      return;
    }
    operation_cancelled_ = false;
    action_transitioned_ = false;
    active_action_slot_ = slot;
    operation_request_ = SelectedSemanticRequestV1::Create(
        *document_, *cache, *node, epoch, deadline, runner_,
        base::BindOnce(&Impl::OnPressPrepared, weak_factory_.GetWeakPtr(),
                       slot, std::move(callback)));
  }

  void Verify(uint64_t epoch,
              base::TimeTicks deadline,
              VerifyCompletion callback) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence() && callback);
    auto fail = [&](WebSelectedSemanticOperationStatusV1 status) {
      WebSelectedSemanticVerifyV1 result;
      result.status = status;
      runner_->PostTask(FROM_HERE,
                        base::BindOnce(std::move(callback), std::move(result)));
    };
    Text* text = status_text_.Get();
    Element* parent = status_parent_.Get();
    if (operation_request_ || !press_started_ || deadline.is_null() ||
        base::TimeTicks::Now() >= deadline || !text || !parent ||
        text->parentElement() != parent || !text->isConnected() ||
        &text->GetDocument() != document_.Get()) {
      fail(WebSelectedSemanticOperationStatusV1::kStaleDocument);
      return;
    }
    auto* cache = CurrentCache();
    if (!cache) {
      fail(WebSelectedSemanticOperationStatusV1::kStaleDocument);
      return;
    }
    operation_cancelled_ = false;
    operation_request_ = SelectedSemanticRequestV1::Create(
        *document_, *cache, *text, epoch, deadline, runner_,
        base::BindOnce(&Impl::OnVerifyPrepared, weak_factory_.GetWeakPtr(),
                       WrapWeakPersistent(text), WrapWeakPersistent(parent),
                       std::move(callback)));
  }

  void Cancel() {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    operation_cancelled_ = true;
    if (operation_request_)
      operation_request_->Cancel();
    Revoke(WebSelectedSemanticTerminalV1::kCancelled);
  }

  bool HasLiveButtonSlot(uint64_t slot) const {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    const auto device_scale = CurrentPureDeviceScale();
    if (!slot || !request_ || !captured_tree_version_ ||
        !captured_style_version_ || !captured_layout_generation_ ||
        !captured_device_scale_ || !device_scale ||
        *captured_device_scale_ != *device_scale ||
        !HasCurrentDocument() || !document_->View() ||
        document_->DomTreeVersion() != *captured_tree_version_ ||
        document_->StyleVersion() != *captured_style_version_ ||
        document_->View()->LayoutGenerationForSelectedSemantic() !=
            *captured_layout_generation_ ||
        !request_->HasCurrentScrollGeometry() ||
        !capture_cache_ ||
        document_->ExistingAXObjectCache() != capture_cache_.Get()) {
      return false;
    }
    for (const auto& binding : slots_) {
      if (binding.slot == slot)
        return !binding.used && HasCurrentButtonFacts(binding);
    }
    return false;
  }

 private:
  struct CaptureState {
    std::optional<WebSelectedSemanticTerminalV1> revoked;
    base::TimeTicks deadline;
    std::optional<float> device_scale;
  };
  struct SlotBinding {
    uint64_t slot;
    WeakPersistent<Node> node;
    WeakPersistent<Node> parent;
    String label;
    gfx::Rect bounds;
    bool used = false;
  };

  SlotBinding* FindSlot(uint64_t slot) {
    for (auto& binding : slots_) {
      if (binding.slot == slot)
        return &binding;
    }
    return nullptr;
  }

  bool HasCurrentDocument() const {
    return document_ && frame_ && document_token_ && frame_token_ &&
           document_->IsActive() && frame_->IsAttached() &&
           frame_->IsMainFrame() && frame_->GetDocument() == document_.Get() &&
           document_->GetFrame() == frame_.Get() &&
           document_->Token() == *document_token_ &&
           frame_->GetLocalFrameToken() == *frame_token_;
  }

  std::optional<float> CurrentPureDeviceScale() const {
    if (!HasCurrentDocument())
      return std::nullopt;
    Page* page = document_->GetPage();
    if (!page)
      return std::nullopt;
    const float device_scale =
        page->GetChromeClient().ZoomFactorForViewportLayout();
    if (!std::isfinite(device_scale) || device_scale <= 0 ||
        frame_->LayoutZoomFactor() != device_scale ||
        frame_->CssZoomFactor() != 1 ||
        page->GetChromeClient().UserZoomFactor(frame_.Get()) != 1) {
      return std::nullopt;
    }
    return device_scale;
  }

  bool IsCurrentButtonSource(Node* node) const {
    auto* element = DynamicTo<Element>(node);
    return element && element->HasTagName(html_names::kButtonTag) &&
           node->isConnected() && &node->GetDocument() == document_.Get();
  }

  AXObjectCacheImpl* CurrentCache() const {
    if (!HasCurrentDocument() || !capture_cache_ ||
        document_->ExistingAXObjectCache() != capture_cache_.Get()) {
      return nullptr;
    }
    return capture_cache_.Get();
  }

  bool HasCurrentButtonFacts(const SlotBinding& binding) const {
    auto* button = DynamicTo<HTMLButtonElement>(binding.node.Get());
    return button && IsCurrentButtonSource(button) &&
           button->parentNode() == binding.parent.Get() &&
           button->FastGetAttribute(html_names::kAriaLabelAttr) ==
               binding.label &&
           button->GetLayoutObject() &&
           button->GetLayoutObject()->AbsoluteBoundingBoxRect() ==
               binding.bounds;
  }

  bool ContinueGuardedAction(uint64_t slot) {
    SlotBinding* binding = FindSlot(slot);
    // Focus and simulated-click activation bump style and layout generations
    // without a page-script mutation. Label, geometry, parent, and document
    // identity remain the revalidation. A real label or bounds change still
    // fails closed through HasCurrentButtonFacts.
    if (!binding || operation_cancelled_ || !action_tree_version_ ||
        !HasCurrentDocument() || !document_->View() ||
        document_->DomTreeVersion() != *action_tree_version_ ||
        !HasCurrentButtonFacts(*binding)) {
      return false;
    }
    if (!action_transitioned_) {
      if (binding->used || active_action_slot_ != slot)
        return false;
      binding->used = true;
      action_transitioned_ = true;
    }
    return true;
  }

  void OnPressPrepared(uint64_t slot,
                       PressCompletion callback,
                       SemanticRequestResultV1 prepared) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    WebSelectedSemanticPressV1 result;
    SlotBinding* binding = FindSlot(slot);
    if (prepared.terminal == SemanticRequestTerminalV1::kCancelled ||
        operation_cancelled_) {
      result.status =
          WebSelectedSemanticOperationStatusV1::kCancelledBeforeActuation;
    } else if (prepared.terminal !=
               SemanticRequestTerminalV1::kPolicyResult) {
      result.status =
          prepared.terminal == SemanticRequestTerminalV1::kStaleContext
              ? WebSelectedSemanticOperationStatusV1::kStaleDocument
              : WebSelectedSemanticOperationStatusV1::kNotReady;
    } else if (prepared.node.disposition !=
               SemanticDispositionV1::kAdmitted) {
      result.status = ToOperationStatus(prepared.node.disposition);
    } else if (!binding || binding->used ||
               prepared.node.text != binding->label ||
               !HasCurrentButtonFacts(*binding)) {
      result.status = WebSelectedSemanticOperationStatusV1::kStaleDocument;
    } else {
      auto* cache = CurrentCache();
      AXObject* object =
          cache && binding->node ? cache->Get(binding->node.Get()) : nullptr;
      if (!object) {
        result.status = WebSelectedSemanticOperationStatusV1::kStaleDocument;
      } else {
        action_tree_version_ = document_->DomTreeVersion();
        action_style_version_ = document_->StyleVersion();
        action_layout_generation_ =
            document_->View()
                ? document_->View()->LayoutGenerationForSelectedSemantic()
                : 0;
        const auto dispatch = object->PerformSelectedSemanticButtonPress(
            base::BindRepeating(&Impl::ContinueGuardedAction,
                                base::Unretained(this), slot));
        if (dispatch == SelectedSemanticPressDispatchResult::kCompleted) {
          result.status = WebSelectedSemanticOperationStatusV1::kOk;
          result.actuation =
              WebSelectedSemanticActuationStateV1::kStarted;
        } else if (dispatch ==
                   SelectedSemanticPressDispatchResult::kStartedAndStopped) {
          result.status =
              WebSelectedSemanticOperationStatusV1::kEffectUncertain;
          result.actuation =
              WebSelectedSemanticActuationStateV1::kStarted;
        } else {
          result.status =
              WebSelectedSemanticOperationStatusV1::kStaleDocument;
        }
      }
    }
    press_started_ |= action_transitioned_;
    operation_request_.reset();
    active_action_slot_ = 0;
    action_transitioned_ = false;
    action_tree_version_.reset();
    action_style_version_.reset();
    action_layout_generation_.reset();
    operation_cancelled_ = false;
    std::move(callback).Run(std::move(result));
  }

  void OnVerifyPrepared(WeakPersistent<Text> expected_text,
                        WeakPersistent<Element> expected_parent,
                        VerifyCompletion callback,
                        SemanticRequestResultV1 prepared) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    WebSelectedSemanticVerifyV1 result;
    Text* text = expected_text.Get();
    Element* parent = expected_parent.Get();
    if (prepared.terminal == SemanticRequestTerminalV1::kCancelled ||
        operation_cancelled_) {
      result.status =
          WebSelectedSemanticOperationStatusV1::kEffectUncertain;
    } else if (prepared.terminal !=
               SemanticRequestTerminalV1::kPolicyResult) {
      result.status =
          prepared.terminal == SemanticRequestTerminalV1::kStaleContext
              ? WebSelectedSemanticOperationStatusV1::kStaleDocument
              : WebSelectedSemanticOperationStatusV1::kNotReady;
    } else if (!text || !parent || text != status_text_.Get() ||
               parent != status_parent_.Get() ||
               text->parentElement() != parent) {
      result.status = WebSelectedSemanticOperationStatusV1::kStaleDocument;
    } else if (prepared.node.disposition !=
               SemanticDispositionV1::kAdmitted) {
      result.status = ToOperationStatus(prepared.node.disposition);
    } else {
      result.status = WebSelectedSemanticOperationStatusV1::kOk;
      result.value = WebString(prepared.node.text);
    }
    operation_request_.reset();
    operation_cancelled_ = false;
    std::move(callback).Run(std::move(result));
  }

  void Revoke(WebSelectedSemanticTerminalV1 reason) {
    if (active_) {
      active_->revoked = reason;
      active_.reset();
    }
    slots_.clear();
    status_text_ = nullptr;
    status_parent_ = nullptr;
    press_started_ = false;
    captured_tree_version_.reset();
    captured_style_version_.reset();
    captured_layout_generation_.reset();
    captured_device_scale_.reset();
    capture_cache_ = nullptr;
    if (request_) {
      request_->Cancel();
      request_.reset();
    }
  }

  void PostResult(std::shared_ptr<CaptureState> state,
                  Completion callback,
                  SemanticRequestResultV1 result) {
    runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&Impl::OnResult, weak_factory_.GetWeakPtr(),
                       std::move(state), std::move(callback), std::move(result)));
  }

  void OnResult(std::shared_ptr<CaptureState> state,
                Completion callback,
                SemanticRequestResultV1 result) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    WebSelectedSemanticCaptureV1 output;
    output.audit = {result.audit.text_reads,
                    result.audit.structural_reads,
                    result.audit.forbidden_reads,
                    result.audit.forbidden_structure_prunes,
                    result.audit.original_zero_size_exclusions,
                    result.audit.original_wholly_offscreen_exclusions};
    output.budget = {result.budget.nodes, result.budget.depth,
                     result.budget.fragments, result.budget.relation_steps,
                     result.budget.utf8_bytes};
    if (state->revoked || state != active_) {
      CloseWithoutPageData(
          output,
          state->revoked.value_or(WebSelectedSemanticTerminalV1::kStaleContext));
      std::move(callback).Run(std::move(output));
      return;
    }
    if (!state->deadline.is_null() &&
        base::TimeTicks::Now() >= state->deadline) {
      active_.reset();
      request_.reset();
      CloseWithoutPageData(output,
                           WebSelectedSemanticTerminalV1::kDeadlineExceeded);
      std::move(callback).Run(std::move(output));
      return;
    }
    active_.reset();
    output.terminal = ToWebTerminal(result.terminal);
    if (result.terminal == SemanticRequestTerminalV1::kPolicyResult) {
      output.disposition = ToWebDisposition(result.observation.disposition);
      if (result.observation.disposition == SemanticDispositionV1::kAdmitted) {
        const auto device_scale = CurrentPureDeviceScale();
        if (!request_ || !state->device_scale || !device_scale ||
            *state->device_scale != *device_scale ||
            !HasCurrentDocument() || !capture_cache_ ||
            document_->ExistingAXObjectCache() != capture_cache_.Get() ||
            !result.document_tree_version || !result.document_style_version ||
            !result.document_layout_generation || !document_->View() ||
            document_->DomTreeVersion() != result.document_tree_version ||
            document_->StyleVersion() != result.document_style_version ||
            document_->View()->LayoutGenerationForSelectedSemantic() !=
                result.document_layout_generation ||
            !request_->HasCurrentScrollGeometry() ||
            result.observation.entries.size() > 256 ||
            result.observation.reserved_output_bytes > 65536) {
          CloseWithoutPageData(output,
                               WebSelectedSemanticTerminalV1::kStaleContext);
        } else {
          PopulateAdmitted(result, *device_scale, output);
        }
      }
    }
    if (output.terminal != WebSelectedSemanticTerminalV1::kPolicyResult ||
        output.disposition != WebSelectedSemanticDispositionV1::kAdmitted) {
      request_.reset();
    }
    std::move(callback).Run(std::move(output));
  }

  void PopulateAdmitted(const SemanticRequestResultV1& result,
                        float device_scale,
                        WebSelectedSemanticCaptureV1& output) {
    auto bindings = request_->TakeButtonBindings();
    auto text_bindings = request_->TakeTextBindings();
    if (next_slot_ > std::numeric_limits<uint64_t>::max() - bindings.size()) {
      CloseWithoutPageData(output,
                           WebSelectedSemanticTerminalV1::kInvalidRequest);
      return;
    }
    std::vector<SlotBinding> next_bindings;
    next_bindings.reserve(bindings.size());
    output.entries.reserve(result.observation.entries.size());
    unsigned binding_index = 0;
    unsigned text_binding_index = 0;
    WeakPersistent<Text> next_status_text;
    WeakPersistent<Element> next_status_parent;
    bool ambiguous_status = false;
    for (unsigned entry_index = 0;
         entry_index < result.observation.entries.size(); ++entry_index) {
      const auto& entry = result.observation.entries[entry_index];
      WebSelectedSemanticEntryV1 web_entry;
      web_entry.role = entry.role == SemanticObservationRoleV1::kText
                           ? WebSelectedSemanticRoleV1::kText
                           : WebSelectedSemanticRoleV1::kButton;
      web_entry.text = WebString(entry.text);
      if (web_entry.role == WebSelectedSemanticRoleV1::kButton) {
        if (binding_index >= bindings.size() ||
            bindings[binding_index].entry_index != entry_index ||
            !IsCurrentButtonSource(bindings[binding_index].node.Get())) {
          CloseWithoutPageData(output,
                               WebSelectedSemanticTerminalV1::kStaleContext);
          return;
        }
        web_entry.button_slot = next_slot_ + binding_index;
        auto* button =
            DynamicTo<HTMLButtonElement>(bindings[binding_index].node.Get());
        if (!button || !button->parentNode() || !button->GetLayoutObject()) {
          CloseWithoutPageData(output,
                               WebSelectedSemanticTerminalV1::kStaleContext);
          return;
        }
        next_bindings.push_back({
            web_entry.button_slot,
            bindings[binding_index].node,
            WeakPersistent<Node>(button->parentNode()),
            button->FastGetAttribute(html_names::kAriaLabelAttr),
            button->GetLayoutObject()->AbsoluteBoundingBoxRect(),
            false,
        });
        ++binding_index;
      } else {
        if (text_binding_index >= text_bindings.size() ||
            text_bindings[text_binding_index].entry_index != entry_index) {
          CloseWithoutPageData(output,
                               WebSelectedSemanticTerminalV1::kStaleContext);
          return;
        }
        auto* text = DynamicTo<Text>(text_bindings[text_binding_index].node.Get());
        if (!text || !text->parentElement()) {
          CloseWithoutPageData(output,
                               WebSelectedSemanticTerminalV1::kStaleContext);
          return;
        }
        Element* parent = text->parentElement();
        if (parent->GetIdAttribute() == AtomicString("action-status") &&
            parent->firstChild() == text && !text->nextSibling()) {
          if (next_status_text) {
            ambiguous_status = true;
          } else {
            next_status_text = text;
            next_status_parent = parent;
          }
        }
        ++text_binding_index;
      }
      output.entries.push_back(std::move(web_entry));
    }
    if (binding_index != bindings.size() ||
        text_binding_index != text_bindings.size()) {
      CloseWithoutPageData(output,
                           WebSelectedSemanticTerminalV1::kInvalidRequest);
      return;
    }
    next_slot_ += bindings.size();
    slots_ = std::move(next_bindings);
    status_text_ = ambiguous_status ? nullptr : next_status_text;
    status_parent_ = ambiguous_status ? nullptr : next_status_parent;
    captured_tree_version_ = result.document_tree_version;
    captured_style_version_ = result.document_style_version;
    captured_layout_generation_ = result.document_layout_generation;
    captured_device_scale_ = device_scale;
    output.reserved_output_bytes = result.observation.reserved_output_bytes;
  }

  WeakPersistent<Document> document_;
  WeakPersistent<LocalFrame> frame_;
  WeakPersistent<AXObjectCacheImpl> capture_cache_;
  std::optional<DocumentToken> document_token_;
  std::optional<LocalFrameToken> frame_token_;
  std::optional<uint64_t> captured_tree_version_;
  std::optional<uint64_t> captured_style_version_;
  std::optional<uint64_t> captured_layout_generation_;
  std::optional<float> captured_device_scale_;
  scoped_refptr<base::SingleThreadTaskRunner> runner_;
  std::unique_ptr<SelectedSemanticRequestV1> request_;
  std::unique_ptr<SelectedSemanticRequestV1> operation_request_;
  std::shared_ptr<CaptureState> active_;
  std::vector<SlotBinding> slots_;
  WeakPersistent<Text> status_text_;
  WeakPersistent<Element> status_parent_;
  uint64_t next_slot_ = 1;
  uint64_t active_action_slot_ = 0;
  bool operation_cancelled_ = false;
  bool action_transitioned_ = false;
  bool press_started_ = false;
  std::optional<uint64_t> action_tree_version_;
  std::optional<uint64_t> action_style_version_;
  std::optional<uint64_t> action_layout_generation_;
  base::WeakPtrFactory<Impl> weak_factory_{this};
};

WebSelectedSemanticSession::WebSelectedSemanticSession(
    WebDocument document,
    scoped_refptr<base::SingleThreadTaskRunner> runner)
    : impl_(std::make_unique<Impl>(std::move(document), std::move(runner))) {}

WebSelectedSemanticSession::~WebSelectedSemanticSession() = default;

void WebSelectedSemanticSession::Capture(uint64_t epoch,
                                         base::TimeTicks deadline,
                                         Completion callback) {
  impl_->Capture(epoch, deadline, std::move(callback));
}

void WebSelectedSemanticSession::Press(uint64_t button_slot,
                                       uint64_t epoch,
                                       base::TimeTicks deadline,
                                       PressCompletion callback) {
  impl_->Press(button_slot, epoch, deadline, std::move(callback));
}

void WebSelectedSemanticSession::Verify(uint64_t epoch,
                                        base::TimeTicks deadline,
                                        VerifyCompletion callback) {
  impl_->Verify(epoch, deadline, std::move(callback));
}

void WebSelectedSemanticSession::Cancel() {
  impl_->Cancel();
}

bool WebSelectedSemanticSession::HasLiveButtonSlot(uint64_t slot) const {
  return impl_->HasLiveButtonSlot(slot);
}

}  // namespace blink
