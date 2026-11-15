// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/title_tidier.h"

#include <stddef.h>

#include <array>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"

namespace aerie_tab_model {

namespace {

// A word that stands alone between spaces and is one of these separates two
// parts of a title.
bool IsSeparatorWord(std::string_view word) {
  return word == "-" || word == "|" || word == "\xE2\x80\x94" /* em dash */ ||
         word == "\xC2\xB7" /* middle dot */;
}

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

// Splits on ASCII white space and on the no-break space (U+00A0). Both are
// separators of words only; every other byte, including every multibyte
// character, stays inside a word.
std::vector<std::string_view> SplitWords(std::string_view text) {
  std::vector<std::string_view> words;
  size_t start = std::string_view::npos;
  size_t i = 0;
  while (i < text.size()) {
    size_t space_length = 0;
    if (IsSpace(text[i])) {
      space_length = 1;
    } else if (static_cast<unsigned char>(text[i]) == 0xC2 &&
               i + 1 < text.size() &&
               static_cast<unsigned char>(text[i + 1]) == 0xA0) {
      space_length = 2;
    }
    if (space_length) {
      if (start != std::string_view::npos) {
        words.push_back(text.substr(start, i - start));
        start = std::string_view::npos;
      }
      i += space_length;
      continue;
    }
    if (start == std::string_view::npos) {
      start = i;
    }
    ++i;
  }
  if (start != std::string_view::npos) {
    words.push_back(text.substr(start));
  }
  return words;
}

// "(3)", "[12]", "(99+)": brackets round up to three digits and an optional
// plus. Not "(a)", not "(2024-05)" and not a year such as "(1982)".
bool IsCountWord(std::string_view word) {
  if (word.size() < 3 || word.size() > 7) {
    return false;
  }
  const char open = word.front();
  const char close = word.back();
  if (!((open == '(' && close == ')') || (open == '[' && close == ']'))) {
    return false;
  }
  std::string_view inner = word.substr(1, word.size() - 2);
  if (inner.back() == '+') {
    inner.remove_suffix(1);
  }
  if (inner.empty() || inner.size() > 3) {
    return false;
  }
  for (char c : inner) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

// A title as parts and the separator word that came before each part (the
// first has none).
struct Parts {
  std::vector<std::vector<std::string_view>> parts;
  std::vector<std::string_view> separators;
};

Parts SplitParts(const std::vector<std::string_view>& words) {
  Parts result;
  std::vector<std::string_view> current;
  // The separator that stands before the part being read.
  std::string_view separator_before;
  for (std::string_view word : words) {
    if (!IsSeparatorWord(word)) {
      current.push_back(word);
      continue;
    }
    if (current.empty()) {
      // A separator at the start, or a repeated one: the first of a run stays.
      continue;
    }
    result.parts.push_back(std::move(current));
    result.separators.push_back(separator_before);
    current.clear();
    separator_before = word;
  }
  // A separator at the end is never kept: nothing follows it.
  if (!current.empty()) {
    result.parts.push_back(std::move(current));
    result.separators.push_back(separator_before);
  }
  return result;
}

std::string JoinParts(const Parts& parts, size_t count) {
  std::string out;
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) {
      out += ' ';
      out += parts.separators[i];
      out += ' ';
    }
    for (size_t w = 0; w < parts.parts[i].size(); ++w) {
      if (w > 0) {
        out += ' ';
      }
      out += parts.parts[i][w];
    }
  }
  return out;
}

std::string Lower(std::string_view text) {
  return base::ToLowerASCII(text);
}

// The text without spaces and punctuation, lower case, for comparing a site's
// name with a part of its host.
std::string Squash(std::string_view text) {
  std::string out;
  for (char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 0x80) {
      out += c;
    } else if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c)) {
      out += base::ToLowerASCII(c);
    }
  }
  return out;
}

// Names that do not follow from a host's labels.
struct HostName {
  std::string_view domain;
  std::string_view name;
};
constexpr std::array<HostName, 14> kHostNames = {{
    {"mail.google.com", "gmail"},
    {"docs.google.com", "googledocs"},
    {"drive.google.com", "googledrive"},
    {"calendar.google.com", "googlecalendar"},
    {"meet.google.com", "googlemeet"},
    {"maps.google.com", "googlemaps"},
    {"news.ycombinator.com", "hackernews"},
    {"stackoverflow.com", "stackoverflow"},
    {"web.whatsapp.com", "whatsapp"},
    {"outlook.live.com", "outlook"},
    {"outlook.office.com", "outlook"},
    {"proton.me", "proton"},
    {"nytimes.com", "thenewyorktimes"},
    {"theguardian.com", "theguardian"},
}};

bool HostIsOrUnder(std::string_view host, std::string_view domain) {
  if (host == domain) {
    return true;
  }
  return host.size() > domain.size() && base::EndsWith(host, domain) &&
         host[host.size() - domain.size() - 1] == '.';
}

bool IsIpHost(std::string_view host) {
  if (host.empty()) {
    return false;
  }
  return host.front() == '[' ||
         base::ContainsOnlyChars(host, "0123456789.");
}

}  // namespace

bool NameMatchesHost(std::string_view name, std::string_view host) {
  if (name.empty() || name.size() > 60 || host.empty() || IsIpHost(host)) {
    return false;
  }
  const std::string squashed = Squash(name);
  if (squashed.size() < 2) {
    return false;
  }
  const std::string lower_host = Lower(host);
  for (const HostName& entry : kHostNames) {
    if (HostIsOrUnder(lower_host, entry.domain) && squashed == entry.name) {
      return true;
    }
  }
  // Each label of the host except the last (the TLD), and for a two-part TLD
  // such as co.uk the one before it too. "BBC News" starts with the label
  // "bbc"; "GitHub" is the label "github".
  std::vector<std::string_view> labels;
  std::string_view rest = lower_host;
  while (true) {
    const size_t dot = rest.find('.');
    if (dot == std::string_view::npos) {
      break;
    }
    labels.push_back(rest.substr(0, dot));
    rest.remove_prefix(dot + 1);
  }
  for (std::string_view label : labels) {
    if (label == "www" || label.size() < 3) {
      continue;
    }
    std::string squashed_label = Squash(label);
    if (squashed == squashed_label) {
      return true;
    }
    // "BBC News" for bbc.co.uk, "GitHub Docs" for github.com: the name starts
    // with the label and carries at most one more word.
    if (squashed.size() > squashed_label.size() &&
        base::StartsWith(squashed, squashed_label) &&
        name.find(' ') != std::string_view::npos &&
        base::StartsWith(Lower(name), squashed_label)) {
      return true;
    }
  }
  return false;
}

std::string CleanSeparators(std::string_view text) {
  const std::vector<std::string_view> words = SplitWords(text);
  const Parts parts = SplitParts(words);
  return JoinParts(parts, parts.parts.size());
}

std::string_view BoundTitle(std::string_view title) {
  if (title.size() <= kMaxTidyTitleBytes) {
    return title;
  }
  // Cut at a character boundary: never in the middle of a UTF-8 sequence.
  size_t cut = kMaxTidyTitleBytes;
  while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return title.substr(0, cut);
}

std::string TidyTitle(std::string_view title, std::string_view host) {
  title = BoundTitle(title);
  std::vector<std::string_view> words = SplitWords(title);
  // A leading count, then (after the site name goes) a trailing one.
  if (words.size() > 1 && IsCountWord(words.front())) {
    words.erase(words.begin());
  }
  Parts parts = SplitParts(words);
  if (parts.parts.empty()) {
    return std::string(title);
  }
  size_t count = parts.parts.size();
  if (count >= 2) {
    std::string last;
    for (size_t w = 0; w < parts.parts[count - 1].size(); ++w) {
      if (w > 0) {
        last += ' ';
      }
      last += parts.parts[count - 1][w];
    }
    if (NameMatchesHost(last, host)) {
      --count;
    }
  }
  // A trailing count of the last part that stays.
  std::vector<std::string_view>& last_part = parts.parts[count - 1];
  if (last_part.size() > 1 && IsCountWord(last_part.back())) {
    last_part.pop_back();
  } else if (last_part.size() == 1 && IsCountWord(last_part.back()) &&
             count > 1) {
    // "Inbox - (3)" leaves the count alone on its part: drop the part.
    --count;
  }
  const std::string result = JoinParts(parts, count);
  return result.empty() ? std::string(title) : result;
}

}  // namespace aerie_tab_model
