// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_

#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

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

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_
