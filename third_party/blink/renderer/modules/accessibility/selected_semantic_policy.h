// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_

#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

#include <cstdint>
#include <memory>
#include <optional>
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/time/tick_clock.h"
#include "base/timer/timer.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class AXObject;
class AXObjectCacheImpl;
class Node;

enum class SemanticDispositionV1 {
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

struct SemanticBudgetV1 {
  unsigned nodes = 0;
  unsigned depth = 0;
  unsigned fragments = 0;
  unsigned relation_steps = 0;
  unsigned utf8_bytes = 0;
};

MODULES_EXPORT SemanticDispositionV1 ClassifyOwnGeometryV1(
    AXObject&,
    SemanticBudgetV1&);

struct SemanticAuditV1 {
  unsigned text_reads = 0;
  unsigned structural_reads = 0;
  unsigned forbidden_reads = 0;
};

struct SemanticNodeResultV1 {
  SemanticDispositionV1 disposition = SemanticDispositionV1::kNotReady;
  String text;
};

MODULES_EXPORT SemanticNodeResultV1 ReadSelectedSemanticNodeV1(
    AXObject&, SemanticBudgetV1&, SemanticAuditV1&);

MODULES_EXPORT SemanticDispositionV1 ClassifySelectedStructureV1(
    Node&, AXObjectCacheImpl&, SemanticBudgetV1&, SemanticAuditV1&);

class Document;
class LocalFrame;

enum class SemanticRequestTerminalV1 {
  kPolicyResult, kCancelled, kDeadlineExceeded, kStaleContext, kInvalidRequest
};
enum class SemanticRequestKindV1 { kNode, kDocument };
enum class SemanticObservationRoleV1 { kText, kButton };
struct SemanticObservationEntryV1 {
  SemanticObservationRoleV1 role;
  String text;
};
struct SemanticObservationV1 {
  SemanticDispositionV1 disposition = SemanticDispositionV1::kNotReady;
  Vector<SemanticObservationEntryV1> entries;
  unsigned reserved_output_bytes = 0;
};
struct SemanticRequestResultV1 {
  SemanticRequestTerminalV1 terminal = SemanticRequestTerminalV1::kInvalidRequest;
  SemanticRequestKindV1 kind = SemanticRequestKindV1::kNode;
  uint64_t epoch = 0;
  SemanticNodeResultV1 node;
  SemanticObservationV1 observation;
  SemanticBudgetV1 budget;
  SemanticAuditV1 audit;
};

// One renderer-main-sequence request, with no CEF/browser identity authority.
// Completion delivery requires the supplied main-thread runner to keep accepting
// and running tasks through terminal delivery. At most one completion is posted;
// shutdown/rejected posting has no synchronous fallback. The owner must retain
// an operational runner until quiescence in the future service integration.
class MODULES_EXPORT SelectedSemanticRequestV1 final {
 public:
  using Completion = base::OnceCallback<void(SemanticRequestResultV1)>;
  static std::unique_ptr<SelectedSemanticRequestV1> Create(
      Document&, AXObjectCacheImpl&, Node&, uint64_t epoch,
      base::TimeTicks deadline,
      scoped_refptr<base::SingleThreadTaskRunner>, Completion);
  static std::unique_ptr<SelectedSemanticRequestV1> CreateDocument(
      Document&, AXObjectCacheImpl&, uint64_t epoch,
      base::TimeTicks deadline,
      scoped_refptr<base::SingleThreadTaskRunner>, Completion);
  ~SelectedSemanticRequestV1();
  SelectedSemanticRequestV1(const SelectedSemanticRequestV1&) = delete;
  SelectedSemanticRequestV1& operator=(const SelectedSemanticRequestV1&) = delete;
  void Cancel();
  void InvalidateEpoch(uint64_t current_epoch);

 private:
  friend class SelectedSemanticRequestTestPeer;
  SelectedSemanticRequestV1(Document&, AXObjectCacheImpl&, Node&, uint64_t,
                            base::TimeTicks,
                            scoped_refptr<base::SingleThreadTaskRunner>, Completion,
                            const base::TickClock*,
                            SemanticRequestKindV1 = SemanticRequestKindV1::kNode);
  void Start();
  void OnAXReady();
  void OnDeadline();
  bool HasCurrentOwnership() const;
  void Finish(SemanticRequestResultV1);

  WeakPersistent<Document> document_;
  WeakPersistent<LocalFrame> frame_;
  WeakPersistent<Node> node_;
  WeakPersistent<AXObjectCacheImpl> cache_;
  std::optional<DocumentToken> document_token_;
  std::optional<LocalFrameToken> frame_token_;
  const base::TickClock* const clock_;
  const uint64_t epoch_;
  const SemanticRequestKindV1 kind_;
  const base::TimeTicks deadline_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  Completion completion_;
  bool terminal_ = false;
  base::OneShotTimer timer_;
  base::WeakPtrFactory<SelectedSemanticRequestV1> weak_factory_{this};
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_
