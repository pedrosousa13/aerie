// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_engine.h"

#include <string_view>

#include "base/containers/span_rust.h"
#include "base/strings/string_util.h"

namespace aerie_adblock {

namespace {

RequestType ToRustType(RequestKind kind) {
  switch (kind) {
    case RequestKind::kOther:
      return RequestType::Other;
    case RequestKind::kDocument:
      return RequestType::Document;
    case RequestKind::kSubdocument:
      return RequestType::Subdocument;
    case RequestKind::kScript:
      return RequestType::Script;
    case RequestKind::kStylesheet:
      return RequestType::Stylesheet;
    case RequestKind::kImage:
      return RequestType::Image;
    case RequestKind::kFont:
      return RequestType::Font;
    case RequestKind::kMedia:
      return RequestType::Media;
    case RequestKind::kXmlHttpRequest:
      return RequestType::XmlHttpRequest;
    case RequestKind::kPing:
      return RequestType::Ping;
    case RequestKind::kWebsocket:
      return RequestType::Websocket;
    case RequestKind::kObject:
      return RequestType::Object;
  }
  return RequestType::Other;
}

// rust::Str throws on invalid UTF-8, and Chromium builds without exceptions.
// ASCII is always valid UTF-8, so check that and nothing else.
bool IsPassable(const ClassifiedRequest& request) {
  return base::IsStringASCII(request.url) &&
         base::IsStringASCII(request.hostname) &&
         base::IsStringASCII(request.source_hostname);
}

rust::Str ToStr(std::string_view text) {
  return rust::Str(text.data(), text.size());
}

}  // namespace

AdblockEngine::AdblockEngine() : engine_(new_empty_engine()) {}

AdblockEngine::~AdblockEngine() = default;

bool AdblockEngine::LoadSerialized(base::span<const uint8_t> data) {
  return engine_->load_serialized(base::SpanToRustSlice(data));
}

void AdblockEngine::LoadListForTesting(std::string_view list) {
  engine_ = new_engine_from_list(base::SpanToRustSlice(base::as_byte_span(list)));
}

std::vector<uint8_t> AdblockEngine::SerializeForTesting() const {
  rust::Vec<uint8_t> bytes = engine_->serialize();
  return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

bool AdblockEngine::ShouldBlock(const ClassifiedRequest& request) const {
  if (!IsPassable(request)) {
    return false;
  }
  return engine_->should_block(
      rust::Str(request.url.data(), request.url.size()),
      rust::Str(request.hostname.data(), request.hostname.size()),
      rust::Str(request.source_hostname.data(), request.source_hostname.size()),
      ToRustType(request.kind), request.third_party);
}

std::string AdblockEngine::CosmeticStylesheet(std::string_view page_url) const {
  if (!base::IsStringASCII(page_url)) {
    return std::string();
  }
  rust::String css = engine_->cosmetic_stylesheet(ToStr(page_url));
  return std::string(css.data(), css.size());
}

std::string AdblockEngine::GenericStylesheet(std::string_view page_url,
                                             std::string_view classes,
                                             std::string_view ids) const {
  if (!base::IsStringASCII(page_url) || !base::IsStringASCII(classes) ||
      !base::IsStringASCII(ids)) {
    return std::string();
  }
  rust::String css = engine_->generic_stylesheet(ToStr(page_url),
                                                 ToStr(classes), ToStr(ids));
  return std::string(css.data(), css.size());
}

}  // namespace aerie_adblock
