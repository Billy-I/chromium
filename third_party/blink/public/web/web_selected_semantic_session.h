// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_PUBLIC_WEB_WEB_SELECTED_SEMANTIC_SESSION_H_
#define THIRD_PARTY_BLINK_PUBLIC_WEB_WEB_SELECTED_SEMANTIC_SESSION_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "third_party/blink/public/platform/web_common.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace blink {

enum class WebSelectedSemanticTerminalV1 {
  kPolicyResult,
  kCancelled,
  kDeadlineExceeded,
  kStaleContext,
  kInvalidRequest,
};

enum class WebSelectedSemanticDispositionV1 {
  kAdmitted,
  kForbiddenAncestor,
  kOwnZeroSize,
  kOffscreen,
  kUnsupportedGeometry,
  kUnsupportedText,
  kStaleDocument,
  kNotReady,
  kLimitExceeded,
};

enum class WebSelectedSemanticRoleV1 { kText, kButton };

struct WebSelectedSemanticEntryV1 {
  WebSelectedSemanticRoleV1 role = WebSelectedSemanticRoleV1::kText;
  WebString text;
  // Renderer-local opaque association. Zero means there is no target.
  uint64_t button_slot = 0;
};

struct WebSelectedSemanticAuditV1 {
  unsigned text_reads = 0;
  unsigned structural_reads = 0;
  unsigned forbidden_reads = 0;
  // One visited forbidden structural branch, counted without its descendants.
  unsigned forbidden_structure_prunes = 0;
  // One text/button candidate excluded by its own zero-size geometry.
  unsigned original_zero_size_exclusions = 0;
  // One candidate whose positive original geometry has no intersection after
  // applicable ancestor clips and the root viewport. Partial clips and
  // unverified mappings are not counted.
  unsigned original_wholly_offscreen_exclusions = 0;
};

struct WebSelectedSemanticBudgetV1 {
  unsigned nodes = 0;
  unsigned depth = 0;
  unsigned fragments = 0;
  unsigned relation_steps = 0;
  unsigned utf8_bytes = 0;
};

struct WebSelectedSemanticCaptureV1 {
  WebSelectedSemanticTerminalV1 terminal =
      WebSelectedSemanticTerminalV1::kInvalidRequest;
  WebSelectedSemanticDispositionV1 disposition =
      WebSelectedSemanticDispositionV1::kNotReady;
  std::vector<WebSelectedSemanticEntryV1> entries;
  WebSelectedSemanticAuditV1 audit;
  WebSelectedSemanticBudgetV1 budget;
  unsigned reserved_output_bytes = 0;
};

// Frame-owned renderer facade. No DOM/AX pointer is exposed by this interface.
// The caller keeps the main sequence runner alive through posted completion.
// Destruction drops an undelivered callback. The owning renderer service must
// settle its browser request via its disconnect/teardown path; at-most-once
// delivery here is not an exactly-once browser completion guarantee.
class BLINK_EXPORT WebSelectedSemanticSession final {
 public:
  using Completion = base::OnceCallback<void(WebSelectedSemanticCaptureV1)>;
  WebSelectedSemanticSession(WebDocument,
                             scoped_refptr<base::SingleThreadTaskRunner>);
  ~WebSelectedSemanticSession();
  WebSelectedSemanticSession(const WebSelectedSemanticSession&) = delete;
  WebSelectedSemanticSession& operator=(const WebSelectedSemanticSession&) =
      delete;

  void Capture(uint64_t epoch, base::TimeTicks deadline, Completion);
  void Cancel();
  // This is a liveness/ownership check, not action authorization. A future
  // action must re-run the full policy at its own clean checkpoint.
  bool HasLiveButtonSlot(uint64_t slot) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_WEB_WEB_SELECTED_SEMANTIC_SESSION_H_
