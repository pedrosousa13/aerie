// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_engine.h"

#include <algorithm>
#include <string>
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

// The whitespace separated tokens of |list| that are ASCII (and hold no NUL),
// joined with one space. Non-ASCII text never goes to Rust, and a bad token
// costs only itself.
std::string AsciiTokens(std::string_view list) {
  constexpr std::string_view kSeparators = " \t\n\r\f";
  std::string out;
  while (!list.empty()) {
    size_t start = list.find_first_not_of(kSeparators);
    if (start == std::string_view::npos) {
      break;
    }
    list.remove_prefix(start);
    std::string_view token = list.substr(0, list.find_first_of(kSeparators));
    list.remove_prefix(token.size());
    if (base::IsStringASCII(token) &&
        token.find('\0') == std::string_view::npos) {
      if (!out.empty()) {
        out.push_back(' ');
      }
      out.append(token);
    }
  }
  return out;
}

ListFailure FromRust(ListStatus status) {
  switch (status) {
    case ListStatus::Ok:
      return ListFailure::kNone;
    case ListStatus::TooLarge:
      return ListFailure::kTooLarge;
    case ListStatus::Timeout:
      return ListFailure::kTimeout;
    case ListStatus::NotText:
      return ListFailure::kNotText;
    case ListStatus::LooksLikeHtml:
      return ListFailure::kLooksLikeHtml;
    case ListStatus::NoRules:
      return ListFailure::kNoRules;
  }
  return ListFailure::kNotText;
}

uint32_t BudgetMs(base::TimeDelta budget) {
  return static_cast<uint32_t>(
      std::clamp<int64_t>(budget.InMilliseconds(), 0, 10 * 60 * 1000));
}

}  // namespace

ListCheck CheckList(base::span<const uint8_t> text, base::TimeDelta budget) {
  ListReport report =
      check_list(base::SpanToRustSlice(text), BudgetMs(budget));
  ListCheck check;
  check.failure = FromRust(report.status);
  if (check.failure == ListFailure::kNone) {
    check.rules = static_cast<int>(
        std::min<uint32_t>(report.network_rules, 100'000'000));
    check.ignored_exceptions = static_cast<int>(
        std::min<uint32_t>(report.ignored_exceptions, 100'000'000));
    check.title = std::string(report.title);
  }
  return check;
}

BuildResult AdblockEngine::LoadLists(const std::vector<std::string>& lists,
                                     base::TimeDelta budget) {
  rust::Box<EngineBuilder> builder = new_engine_builder();
  for (const std::string& list : lists) {
    builder->add_list(base::SpanToRustSlice(base::as_byte_span(list)));
  }
  BuildReport report = builder->build_into(BudgetMs(budget), *engine_);
  BuildResult result;
  result.failure = FromRust(report.status);
  result.failed_index = report.failed_index;
  if (result.failure == ListFailure::kNone) {
    result.ignored_exceptions = report.ignored_exceptions;
  }
  return result;
}

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
  if (!base::IsStringASCII(page_url)) {
    return std::string();
  }
  // The lists come from a page, so a token that is not ASCII is skipped on its
  // own: it never reaches Rust, and it does not void the other tokens.
  const std::string ascii_classes = AsciiTokens(classes);
  const std::string ascii_ids = AsciiTokens(ids);
  rust::String css = engine_->generic_stylesheet(
      ToStr(page_url), ToStr(ascii_classes), ToStr(ascii_ids));
  return std::string(css.data(), css.size());
}

}  // namespace aerie_adblock
