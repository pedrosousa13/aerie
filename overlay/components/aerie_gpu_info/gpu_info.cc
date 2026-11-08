// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_gpu_info/gpu_info.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace aerie_gpu_info {
namespace {

// The two strings joined with a space and lowercased.
std::string JoinLowercase(std::string_view a, std::string_view b) {
  std::string out(a);
  out.push_back(' ');
  out.append(b);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// True if `word` occurs in `text` with no letter or digit on either side.
bool HasWord(const std::string& text, std::string_view word) {
  for (size_t at = text.find(word); at != std::string::npos;
       at = text.find(word, at + 1)) {
    bool before_ok = at == 0 || !IsWordChar(text[at - 1]);
    size_t end = at + word.size();
    bool after_ok = end == text.size() || !IsWordChar(text[end]);
    if (before_ok && after_ok) {
      return true;
    }
  }
  return false;
}

// Mesa's nouveau driver names its chips NV<hex>, for example NV50, NVE4 or
// NV117, or just "NV". The word must be all of that: "nvme" is not one.
bool HasNouveauChipName(const std::string& text) {
  for (size_t at = text.find("nv"); at != std::string::npos;
       at = text.find("nv", at + 1)) {
    if (at > 0 && IsWordChar(text[at - 1])) {
      continue;
    }
    size_t end = at + 2;
    while (end < text.size() &&
           std::isxdigit(static_cast<unsigned char>(text[end])) != 0) {
      ++end;
    }
    if (end == text.size() || !IsWordChar(text[end])) {
      return true;
    }
  }
  return false;
}

bool HasAnyWord(const std::string& text,
                std::initializer_list<std::string_view> words) {
  return std::any_of(words.begin(), words.end(), [&](std::string_view w) {
    return HasWord(text, w);
  });
}

}  // namespace

GpuFamily GpuFamilyFromGlStrings(std::string_view vendor,
                                 std::string_view renderer) {
  const std::string text = JoinLowercase(vendor, renderer);
  if (HasAnyWord(text, {"llvmpipe", "softpipe", "lavapipe"})) {
    return GpuFamily::kMesa;
  }
  if (HasAnyWord(text, {"nvidia", "geforce", "nouveau"}) ||
      HasNouveauChipName(text)) {
    return GpuFamily::kNvidia;
  }
  if (HasAnyWord(text, {"amd", "radeon", "ati"})) {
    return GpuFamily::kAmd;
  }
  if (HasWord(text, "intel")) {
    return GpuFamily::kIntel;
  }
  if (HasWord(text, "mesa")) {
    return GpuFamily::kMesa;
  }
  // Nothing matched (SwiftShader, Apple, an empty string). Chrome never reports
  // an "other" family, so pick one it does. Intel is the most common GPU family
  // among Linux desktop and laptop users: nearly every Intel CPU has an
  // integrated GPU, and many AMD and NVIDIA machines still report Intel when
  // the integrated GPU is the one in use. A string that most Linux users could
  // produce hides the rest in the crowd.
  return GpuFamily::kIntel;
}

std::string MaskedVendor(GpuFamily family) {
  switch (family) {
    case GpuFamily::kAmd:
      return "Google Inc. (AMD)";
    case GpuFamily::kIntel:
      return "Google Inc. (Intel)";
    case GpuFamily::kNvidia:
      return "Google Inc. (NVIDIA)";
    case GpuFamily::kMesa:
      return "Google Inc. (Mesa)";
  }
  // Not reached: the switch covers every family. This component has no base, so
  // there is no NOTREACHED(); the return only keeps -Wreturn-type quiet.
  return "Google Inc. (Intel)";
}

std::string MaskedRenderer(GpuFamily family) {
  switch (family) {
    case GpuFamily::kAmd:
      return "ANGLE (AMD, AMD Radeon Graphics, OpenGL ES 3.2)";
    case GpuFamily::kIntel:
      return "ANGLE (Intel, Mesa Intel(R) Graphics, OpenGL ES 3.2)";
    case GpuFamily::kNvidia:
      return "ANGLE (NVIDIA, NVIDIA GeForce Graphics, OpenGL ES 3.2)";
    case GpuFamily::kMesa:
      return "ANGLE (Mesa, llvmpipe, OpenGL ES 3.2)";
  }
  // Not reached, as in MaskedVendor.
  return "ANGLE (Intel, Mesa Intel(R) Graphics, OpenGL ES 3.2)";
}

bool ShouldMaskGpuInfo(ContextKind kind, const MaskPolicy& policy) {
  if (!policy.master_enabled || !policy.surface_enabled) {
    return false;
  }
  return kind == ContextKind::kWorker || policy.has_token;
}

}  // namespace aerie_gpu_info
