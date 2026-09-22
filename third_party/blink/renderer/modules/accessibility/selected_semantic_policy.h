// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_

#include "third_party/blink/renderer/modules/modules_export.h"

namespace blink {

class AXObject;

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
};

MODULES_EXPORT SemanticDispositionV1 ClassifyOwnGeometryV1(
    AXObject&,
    SemanticBudgetV1&);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_ACCESSIBILITY_SELECTED_SEMANTIC_POLICY_H_
