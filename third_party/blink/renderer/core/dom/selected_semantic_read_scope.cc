// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/dom/selected_semantic_read_scope.h"

#include <limits>

#include "base/check.h"
#include "third_party/blink/renderer/core/dom/attribute.h"
#include "third_party/blink/renderer/core/dom/character_data.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/html/html_element.h"

namespace blink {
namespace {
void IncrementBounded(unsigned& count) {
  if (count != std::numeric_limits<unsigned>::max()) {
    ++count;
  }
}
}  // namespace

thread_local SelectedSemanticReadScope* SelectedSemanticReadScope::current_ = nullptr;

SelectedSemanticReadScope::SelectedSemanticReadScope(const Document& document)
    : previous_(current_), root_(previous_ ? previous_->root_ : this),
      document_(&document) {
  if (root_->document_ != &document) {
    Reject(Violation::kWrongDocument);
  } else if (previous_ && root_->purpose_ != Purpose::kNone) {
    Reject(Violation::kNestedPendingPermit);
  }
  current_ = this;
}

SelectedSemanticReadScope::~SelectedSemanticReadScope() {
  CHECK(current_ == this);
  if (root_->permit_scope_ == this) {
    Reject(Violation::kExpiredPermit);
  }
  current_ = previous_;
}

void SelectedSemanticReadScope::Revoke() {
  root_->source_ = nullptr;
  root_->owner_ = nullptr;
  root_->attribute_ = nullptr;
  root_->permit_scope_ = nullptr;
  root_->attribute_index_ = 0;
  root_->purpose_ = Purpose::kNone;
}

void SelectedSemanticReadScope::Reject(Violation violation) {
  if (root_->violation_ == Violation::kNone) {
    root_->violation_ = violation;
  }
  Revoke();
}

bool SelectedSemanticReadScope::PermitText(const Node& admitted_source,
                                           const CharacterData& text) {
  if (!IsClean()) {
    return false;
  }
  if (current_ != this || root_->purpose_ != Purpose::kNone) {
    Reject(Violation::kInvalidPermit);
    return false;
  }
  if (&admitted_source != &text || !text.IsTextNode() || !text.isConnected()) {
    Reject(Violation::kWrongSource);
    return false;
  }
  if (&text.GetDocument() != root_->document_) {
    Reject(Violation::kWrongDocument);
    return false;
  }
  root_->source_ = &text;
  root_->purpose_ = Purpose::kOwnText;
  root_->permit_scope_ = this;
  return true;
}

bool SelectedSemanticReadScope::PermitAttribute(const Node& admitted_source,
                                                const Element& owner,
                                                unsigned index,
                                                Purpose purpose) {
  if (!IsClean()) {
    return false;
  }
  if (current_ != this || root_->purpose_ != Purpose::kNone) {
    Reject(Violation::kInvalidPermit);
    return false;
  }
  if (&admitted_source != &owner || !owner.isConnected() ||
      !owner.IsHTMLElement()) {
    Reject(Violation::kWrongSource);
    return false;
  }
  if (&owner.GetDocument() != root_->document_) {
    Reject(Violation::kWrongDocument);
    return false;
  }
  const auto attributes = owner.AttributesWithoutUpdate();
  if (attributes.size() > 256 || index >= attributes.size()) {
    Reject(Violation::kInvalidPermit);
    return false;
  }
  const auto& attribute = attributes[index];
  const auto& name = attribute.GetName();
  bool valid = false;
  switch (purpose) {
    case Purpose::kButtonLabel:
      valid = owner.HasTagName(html_names::kButtonTag) &&
              name == html_names::kAriaLabelAttr;
      break;
    case Purpose::kAriaHidden:
      valid = name == html_names::kAriaHiddenAttr;
      break;
    case Purpose::kAriaDisabled:
      valid = name == html_names::kAriaDisabledAttr;
      break;
    case Purpose::kContentEditable:
      valid = name == html_names::kContenteditableAttr;
      break;
    default:
      break;
  }
  if (!valid) {
    Reject(Violation::kInvalidPermit);
    return false;
  }
  root_->source_ = &admitted_source;
  root_->owner_ = &owner;
  root_->attribute_ = &attribute;
  root_->attribute_index_ = index;
  root_->purpose_ = purpose;
  root_->permit_scope_ = this;
  return true;
}

bool SelectedSemanticReadScope::AllowCharacterData(const CharacterData& text) {
  if (!current_) {
    return true;
  }
  auto& scope = *current_;
  auto& root = *scope.root_;
  if (!scope.IsClean()) {
    IncrementBounded(root.counts_.forbidden);
    return false;
  }
  if (root.purpose_ != Purpose::kOwnText || root.permit_scope_ != current_) {
    scope.Reject(Violation::kUnlistedRead);
  } else if (root.source_ != &text || !text.isConnected()) {
    scope.Reject(Violation::kWrongSource);
  } else if (&text.GetDocument() != root.document_) {
    scope.Reject(Violation::kWrongDocument);
  } else {
    scope.Revoke();  // Consume before CharacterData can materialize the string.
    IncrementBounded(root.counts_.content);
    return true;
  }
  IncrementBounded(root.counts_.forbidden);
  return false;
}

bool SelectedSemanticReadScope::AllowAttribute(const Attribute& attribute) {
  if (!current_) {
    return true;
  }
  auto& scope = *current_;
  auto& root = *scope.root_;
  if (!scope.IsClean()) {
    IncrementBounded(root.counts_.forbidden);
    return false;
  }
  const auto purpose = root.purpose_;
  if (purpose == Purpose::kNone || purpose == Purpose::kOwnText ||
      root.permit_scope_ != current_) {
    scope.Reject(Violation::kUnlistedRead);
  } else if (root.attribute_ != &attribute || !root.owner_ ||
             root.source_ != root.owner_ || !root.owner_->isConnected()) {
    scope.Reject(Violation::kWrongSource);
  } else if (&root.owner_->GetDocument() != root.document_) {
    scope.Reject(Violation::kWrongDocument);
  } else {
    const auto attributes = root.owner_->AttributesWithoutUpdate();
    if (root.attribute_index_ >= attributes.size() ||
        &attributes[root.attribute_index_] != &attribute) {
      scope.Reject(Violation::kInvalidPermit);
    } else {
      // The private caller validated owner/name/current membership immediately
      // before this getter. No callback or yielding operation may intervene.
      scope.Revoke();
      IncrementBounded(purpose == Purpose::kButtonLabel ? root.counts_.content
                                                       : root.counts_.structural);
      return true;
    }
  }
  IncrementBounded(root.counts_.forbidden);
  return false;
}
}  // namespace blink
