// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_SELECTED_SEMANTIC_READ_SCOPE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_SELECTED_SEMANTIC_READ_SCOPE_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {
class Attribute;
class CharacterData;
class Document;
class Element;
class Node;
class SelectedSemanticPolicyReadAccess;
class SelectedSemanticPolicyTest;

// Observes the two explicitly inventoried primitive read families. This is not
// a renderer-wide string interceptor. Private callers must remain read-audited.
class CORE_EXPORT SelectedSemanticReadScope final {
  STACK_ALLOCATED();
 public:
  enum class Violation {
    kNone, kUnlistedRead, kWrongSource, kWrongDocument, kInvalidPermit,
    kNestedPendingPermit, kExpiredPermit
  };
  struct Counts {
    unsigned structural = 0;
    unsigned content = 0;
    unsigned forbidden = 0;
  };

  explicit SelectedSemanticReadScope(const Document&);
  ~SelectedSemanticReadScope();
  SelectedSemanticReadScope(const SelectedSemanticReadScope&) = delete;
  SelectedSemanticReadScope& operator=(const SelectedSemanticReadScope&) = delete;
  SelectedSemanticReadScope(SelectedSemanticReadScope&&) = delete;
  SelectedSemanticReadScope& operator=(SelectedSemanticReadScope&&) = delete;

  bool IsClean() const { return root_->violation_ == Violation::kNone; }
  Violation FirstViolation() const { return root_->violation_; }
  const Counts& ReadCounts() const { return root_->counts_; }
  static bool AllowCharacterData(const CharacterData&);
  static bool AllowAttribute(const Attribute&);

 private:
  friend class SelectedSemanticPolicyReadAccess;
  friend class SelectedSemanticPolicyTest;
  enum class Purpose { kNone, kOwnText, kButtonLabel, kAriaHidden,
                       kAriaDisabled, kContentEditable };
  bool PermitText(const Node& admitted_source, const CharacterData&);
  bool PermitAttribute(const Node& admitted_source, const Element& owner,
                       unsigned index, Purpose);
  void Reject(Violation);
  void Revoke();

  static thread_local SelectedSemanticReadScope* current_;
  SelectedSemanticReadScope* previous_;
  SelectedSemanticReadScope* root_;
  const Document* document_;
  const Node* source_ = nullptr;
  const Element* owner_ = nullptr;
  const Attribute* attribute_ = nullptr;
  SelectedSemanticReadScope* permit_scope_ = nullptr;
  unsigned attribute_index_ = 0;
  Purpose purpose_ = Purpose::kNone;
  Violation violation_ = Violation::kNone;
  Counts counts_;
};
}  // namespace blink
#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_SELECTED_SEMANTIC_READ_SCOPE_H_
