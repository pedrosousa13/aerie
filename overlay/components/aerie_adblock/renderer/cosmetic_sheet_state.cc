// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/renderer/cosmetic_sheet_state.h"

#include <initializer_list>

#include "base/strings/string_util.h"
#include "components/aerie_adblock/common/cosmetic_limits.h"

namespace aerie_adblock {

CosmeticSheetState::Sheet::Sheet() = default;
CosmeticSheetState::Sheet::~Sheet() = default;

CosmeticSheetState::CosmeticSheetState() = default;
CosmeticSheetState::~CosmeticSheetState() = default;

std::optional<std::string> CosmeticSheetState::Apply(
    mojom::CosmeticSheetKind kind,
    std::string_view css,
    bool append) {
  Sheet& sheet = kind == mojom::CosmeticSheetKind::kPage ? page_ : generic_;
  if (css.empty() && !append) {
    sheet.text.clear();
    sheet.lines.clear();
    return std::string();
  }
  if (css.empty() || css.size() > kMaxSheetBytes ||
      !base::IsStringASCII(css) || css.find('\0') != std::string_view::npos) {
    return std::nullopt;
  }
  bool changed = !append;  // A replace is always inserted.
  if (!append) {
    sheet.text.clear();
    sheet.lines.clear();
  }
  while (!css.empty()) {
    size_t end = css.find('\n');
    std::string_view line = css.substr(0, end);
    css = end == std::string_view::npos ? std::string_view()
                                        : css.substr(end + 1);
    if (line.empty() || sheet.lines.contains(line)) {
      continue;
    }
    // Stop at the last whole line that fits: a cut could end inside a rule.
    if (sheet.text.size() + line.size() + 1 > kMaxSheetBytes) {
      break;
    }
    sheet.lines.emplace(line);
    sheet.text.append(line);
    sheet.text.push_back('\n');
    changed = true;
  }
  if (!changed) {
    return std::nullopt;
  }
  return sheet.text;
}

const std::string& CosmeticSheetState::Text(
    mojom::CosmeticSheetKind kind) const {
  return (kind == mojom::CosmeticSheetKind::kPage ? page_ : generic_).text;
}

void CosmeticSheetState::Reset() {
  for (Sheet* sheet : {&page_, &generic_}) {
    sheet->text.clear();
    sheet->lines.clear();
  }
}

GenericInsertPacer::GenericInsertPacer() = default;
GenericInsertPacer::~GenericInsertPacer() = default;

GenericInsertPacer::Decision GenericInsertPacer::OnChange(
    base::TimeTicks now) const {
  if (inserts_ >= kMaxGenericInsertsPerDocument) {
    return {Decision::kDrop, base::TimeDelta()};
  }
  const base::TimeDelta interval =
      base::Milliseconds(kGenericReinsertMinIntervalMs);
  if (!last_ || now - *last_ >= interval) {
    return {Decision::kNow, base::TimeDelta()};
  }
  return {Decision::kLater, interval - (now - *last_)};
}

void GenericInsertPacer::OnInserted(base::TimeTicks now) {
  ++inserts_;
  last_ = now;
}

void GenericInsertPacer::Reset() {
  inserts_ = 0;
  last_.reset();
}

}  // namespace aerie_adblock
