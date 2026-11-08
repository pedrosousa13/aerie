// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_storage.h"

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"

namespace aerie_adblock::filter_list_storage {

namespace {
// A state file is a few kilobytes. Anything this large is not ours.
constexpr size_t kMaxStateBytes = 1024 * 1024;
}  // namespace

FilterListState LoadState(const base::FilePath& dir) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(dir.AppendASCII(kStateFile), &text,
                                         kMaxStateBytes)) {
    return FilterListState();
  }
  std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  if (!json || !json->is_dict()) {
    return FilterListState();
  }
  return FilterListState::FromValue(json->GetDict());
}

bool SaveState(const base::FilePath& dir, const FilterListState& state) {
  std::optional<std::string> json = base::WriteJson(state.ToValue());
  if (!json || !base::CreateDirectory(dir)) {
    return false;
  }
  return base::ImportantFileWriter::WriteFileAtomically(
      dir.AppendASCII(kStateFile), *json, "AerieFilterListState");
}

base::FilePath ListPath(const base::FilePath& dir, std::string_view id) {
  if (!IsValidListId(id)) {
    return base::FilePath();
  }
  return dir.AppendASCII(kListsDir).AppendASCII(std::string(id) + ".txt");
}

bool WriteList(const base::FilePath& dir, std::string_view id, std::string_view text) {
  base::FilePath path = ListPath(dir, id);
  if (path.empty() || !base::CreateDirectory(path.DirName())) {
    return false;
  }
  return base::ImportantFileWriter::WriteFileAtomically(path, text,
                                                        "AerieFilterList");
}

void DeleteList(const base::FilePath& dir, std::string_view id) {
  base::FilePath path = ListPath(dir, id);
  if (!path.empty()) {
    base::DeleteFile(path);
  }
}

}  // namespace aerie_adblock::filter_list_storage
