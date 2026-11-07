// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/protection_engine.h"

#include "base/containers/span_rust.h"
#include "base/strings/string_util.h"

namespace aerie_adblock {

ProtectionEngine::ProtectionEngine() : engine_(new_empty_protection_engine()) {}

ProtectionEngine::~ProtectionEngine() = default;

bool ProtectionEngine::LoadSerialized(base::span<const uint8_t> data) {
  return engine_->load_serialized(base::SpanToRustSlice(data));
}

void ProtectionEngine::LoadListForTesting(std::string_view list) {
  engine_ = new_protection_engine_from_list(
      base::SpanToRustSlice(base::as_byte_span(list)));
}

std::vector<uint8_t> ProtectionEngine::SerializeForTesting() const {
  rust::Vec<uint8_t> bytes = engine_->serialize();
  return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

bool ProtectionEngine::IsListed(std::string_view host) const {
  // rust::Str throws on invalid UTF-8, and Chromium builds without
  // exceptions. ASCII is always valid UTF-8.
  if (host.empty() || !base::IsStringASCII(host)) {
    return false;
  }
  return engine_->is_listed(rust::Str(host.data(), host.size()));
}

size_t ProtectionEngine::domain_count() const {
  return engine_->domain_count();
}

}  // namespace aerie_adblock
