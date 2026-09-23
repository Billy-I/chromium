// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/web/web_selected_semantic_session.h"

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
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html_names.h"
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
  }

  void Capture(uint64_t epoch, base::TimeTicks deadline, Completion callback) {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence() && callback);
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
    capture_cache_ = cache;
    request_ = SelectedSemanticRequestV1::CreateDocument(
        *document_, *cache, epoch, deadline, runner_,
        base::BindOnce(&Impl::OnResult, weak_factory_.GetWeakPtr(),
                       std::move(state), std::move(callback)));
  }

  void Cancel() {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    Revoke(WebSelectedSemanticTerminalV1::kCancelled);
  }

  bool HasLiveButtonSlot(uint64_t slot) const {
    CHECK(IsMainThread() && runner_->RunsTasksInCurrentSequence());
    if (!slot || !request_ || !captured_tree_version_ ||
        !captured_style_version_ || !captured_layout_generation_ ||
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
        return IsCurrentButtonSource(binding.node.Get());
    }
    return false;
  }

 private:
  struct CaptureState {
    std::optional<WebSelectedSemanticTerminalV1> revoked;
    base::TimeTicks deadline;
  };
  struct SlotBinding {
    uint64_t slot;
    WeakPersistent<Node> node;
  };

  bool HasCurrentDocument() const {
    return document_ && frame_ && document_token_ && frame_token_ &&
           document_->IsActive() && frame_->IsAttached() &&
           frame_->IsMainFrame() && frame_->GetDocument() == document_.Get() &&
           document_->GetFrame() == frame_.Get() &&
           document_->Token() == *document_token_ &&
           frame_->GetLocalFrameToken() == *frame_token_;
  }

  bool IsCurrentButtonSource(Node* node) const {
    auto* element = DynamicTo<Element>(node);
    return element && element->HasTagName(html_names::kButtonTag) &&
           node->isConnected() && &node->GetDocument() == document_.Get();
  }

  void Revoke(WebSelectedSemanticTerminalV1 reason) {
    if (active_) {
      active_->revoked = reason;
      active_.reset();
    }
    slots_.clear();
    captured_tree_version_.reset();
    captured_style_version_.reset();
    captured_layout_generation_.reset();
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
        if (!request_ || !HasCurrentDocument() || !capture_cache_ ||
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
          PopulateAdmitted(result, output);
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
                        WebSelectedSemanticCaptureV1& output) {
    auto bindings = request_->TakeButtonBindings();
    if (next_slot_ > std::numeric_limits<uint64_t>::max() - bindings.size()) {
      CloseWithoutPageData(output,
                           WebSelectedSemanticTerminalV1::kInvalidRequest);
      return;
    }
    std::vector<SlotBinding> next_bindings;
    next_bindings.reserve(bindings.size());
    output.entries.reserve(result.observation.entries.size());
    unsigned binding_index = 0;
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
        next_bindings.push_back(
            {web_entry.button_slot, bindings[binding_index].node});
        ++binding_index;
      }
      output.entries.push_back(std::move(web_entry));
    }
    if (binding_index != bindings.size()) {
      CloseWithoutPageData(output,
                           WebSelectedSemanticTerminalV1::kInvalidRequest);
      return;
    }
    next_slot_ += bindings.size();
    slots_ = std::move(next_bindings);
    captured_tree_version_ = result.document_tree_version;
    captured_style_version_ = result.document_style_version;
    captured_layout_generation_ = result.document_layout_generation;
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
  scoped_refptr<base::SingleThreadTaskRunner> runner_;
  std::unique_ptr<SelectedSemanticRequestV1> request_;
  std::shared_ptr<CaptureState> active_;
  std::vector<SlotBinding> slots_;
  uint64_t next_slot_ = 1;
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

void WebSelectedSemanticSession::Cancel() {
  impl_->Cancel();
}

bool WebSelectedSemanticSession::HasLiveButtonSlot(uint64_t slot) const {
  return impl_->HasLiveButtonSlot(slot);
}

}  // namespace blink
