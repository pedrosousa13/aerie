// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/title_tidier.h"

#include <string>
#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

struct Row {
  std::string_view title;
  std::string_view host;
  std::string_view expected;
};

// Real-looking titles as sites send them, and what the sidebar shows.
constexpr Row kTidied[] = {
    // Gmail and Outlook.
    {"Inbox - Gmail", "mail.google.com", "Inbox"},
    {"Inbox (3) - Gmail", "mail.google.com", "Inbox"},
    {"(3) Inbox - Gmail", "mail.google.com", "Inbox"},
    {"Inbox (3) - me@gmail.com - Gmail", "mail.google.com",
     "Inbox (3) - me@gmail.com"},
    {"Re: Lunch on Friday? - me@gmail.com - Gmail", "mail.google.com",
     "Re: Lunch on Friday? - me@gmail.com"},
    {"Inbox (12) - Outlook", "outlook.live.com", "Inbox"},
    // GitHub.
    {"pedrosousa13/aerie: A browser · GitHub", "github.com",
     "pedrosousa13/aerie: A browser"},
    {"Issues · pedrosousa13/aerie · GitHub", "github.com",
     "Issues · pedrosousa13/aerie"},
    {"Pull requests | GitHub", "github.com", "Pull requests"},
    {"Fix the crash on start by someone · Pull Request #12 · o/r · GitHub",
     "github.com", "Fix the crash on start by someone · Pull Request #12 · o/r"},
    {"(2) Notifications", "github.com", "Notifications"},
    // YouTube.
    {"Lofi Hip Hop Radio - YouTube", "www.youtube.com", "Lofi Hip Hop Radio"},
    {"(2) Lofi Hip Hop Radio - YouTube", "www.youtube.com",
     "Lofi Hip Hop Radio"},
    {"(99+) Lofi Hip Hop Radio - YouTube", "www.youtube.com",
     "Lofi Hip Hop Radio"},
    {"Rust in 100 Seconds - YouTube", "m.youtube.com", "Rust in 100 Seconds"},
    // Slack.
    {"Acme Corp - general - 3 new items - Slack", "app.slack.com",
     "Acme Corp - general - 3 new items"},
    {"general (3) | Acme Corp | Slack", "app.slack.com",
     "general (3) | Acme Corp"},
    // Reddit.
    {"Best pizza in town? - Reddit", "www.reddit.com", "Best pizza in town?"},
    {"(1) Best pizza in town? - Reddit", "old.reddit.com",
     "Best pizza in town?"},
    // News.
    {"Markets rally as rates fall - BBC News", "www.bbc.co.uk",
     "Markets rally as rates fall"},
    {"Markets rally as rates fall - The New York Times", "www.nytimes.com",
     "Markets rally as rates fall"},
    {"Markets rally as rates fall | The Guardian", "www.theguardian.com",
     "Markets rally as rates fall"},
    {"Show HN: A tiny browser | Hacker News", "news.ycombinator.com",
     "Show HN: A tiny browser"},
    // Reference sites.
    {"Rust (programming language) - Wikipedia", "en.wikipedia.org",
     "Rust (programming language)"},
    {"Apollo 11 - Wikipedia", "en.wikipedia.org", "Apollo 11"},
    {"Blade Runner (1982) - Wikipedia", "en.wikipedia.org",
     "Blade Runner (1982)"},
    {"How do I undo git add? - Stack Overflow", "stackoverflow.com",
     "How do I undo git add?"},
    // Google Workspace.
    {"Budget 2026 - Google Sheets", "docs.google.com", "Budget 2026"},
    {"Meeting notes - Google Docs", "docs.google.com", "Meeting notes"},
    {"Dashboard - Grafana", "grafana.example.com", "Dashboard"},
    // Counts.
    {"[12] Inbox", "example.org", "Inbox"},
    {"Inbox [4]", "example.org", "Inbox"},
    {"Notifications (7)", "example.org", "Notifications"},
    {"(100) Messages", "example.org", "Messages"},
    // Repeated separators and spaces.
    {"Foo - - Bar", "example.org", "Foo - Bar"},
    {"Foo | | Bar |", "example.org", "Foo | Bar"},
    {"  Foo   Bar  ", "example.org", "Foo Bar"},
    {"- Foo -", "example.org", "Foo"},
    {"Foo \xE2\x80\x94 \xE2\x80\x94 Bar", "example.org",
     "Foo \xE2\x80\x94 Bar"},
    {"Foo  \xC2\xB7  Bar", "example.org", "Foo \xC2\xB7 Bar"},
    {"Foo\nBar - GitHub", "github.com", "Foo Bar"},
    {"Foo\xC2\xA0" "Bar", "example.org", "Foo Bar"},
    {"\xE4\xBD\xA0\xE5\xA5\xBD - GitHub", "github.com", "\xE4\xBD\xA0\xE5\xA5\xBD"},
};

// Titles that must not change.
constexpr Row kUnchanged[] = {
    {"Hacker News", "news.ycombinator.com", "Hacker News"},
    {"GitHub", "github.com", "GitHub"},
    {"YouTube", "www.youtube.com", "YouTube"},
    {"GitHub - pedrosousa13/aerie", "github.com",
     "GitHub - pedrosousa13/aerie"},
    {"Mail - Inbox", "mail.example.org", "Mail - Inbox"},
    {"Tom - Jerry", "example.org", "Tom - Jerry"},
    {"Linux - Wikipedia", "example.org", "Linux - Wikipedia"},
    {"Badge - GitHubber", "github.com", "Badge - GitHubber"},
    {"Stack Overflow - Where Developers Learn, Share, & Build Careers",
     "stackoverflow.com",
     "Stack Overflow - Where Developers Learn, Share, & Build Careers"},
    {"Amazon.com : Widget", "www.amazon.com", "Amazon.com : Widget"},
    {"Amazon.com: Widget : Home & Kitchen", "www.amazon.com",
     "Amazon.com: Widget : Home & Kitchen"},
    {"Top 10 movies of (2024)", "example.org", "Top 10 movies of (2024)"},
    {"Chapter (a) of the book", "example.org", "Chapter (a) of the book"},
    {"Version 2.0 (beta)", "example.org", "Version 2.0 (beta)"},
    {"Jack-in-the-box", "example.org", "Jack-in-the-box"},
    {"Home / X", "x.com", "Home / X"},
    {"(3)", "example.org", "(3)"},
    {"-", "example.org", "-"},
    {"", "example.org", ""},
    {"Router - Admin", "192.168.1.1", "Router - Admin"},
    {"Foo - Gmail", "", "Foo - Gmail"},
    {"Hello, world", "example.org", "Hello, world"},
};

}  // namespace

TEST(TitleTidierTest, TidiesTheTable) {
  for (const Row& row : kTidied) {
    EXPECT_EQ(TidyTitle(row.title, row.host), row.expected)
        << "title: " << row.title << " host: " << row.host;
  }
}

TEST(TitleTidierTest, TheTableHasAtLeastFortyTitles) {
  EXPECT_GE(std::size(kTidied) + std::size(kUnchanged), 40u);
}

TEST(TitleTidierTest, LeavesTheMustNotChangeTitlesAlone) {
  for (const Row& row : kUnchanged) {
    EXPECT_EQ(TidyTitle(row.title, row.host), row.expected)
        << "title: " << row.title << " host: " << row.host;
  }
}

TEST(TitleTidierTest, TidyingTwiceChangesNothingMore) {
  for (const Row& row : kTidied) {
    const std::string once = TidyTitle(row.title, row.host);
    EXPECT_EQ(TidyTitle(once, row.host), once) << row.title;
  }
}

TEST(TitleTidierTest, AnIpAddressHasNoSiteName) {
  EXPECT_FALSE(NameMatchesHost("Router", "192.168.1.1"));
  EXPECT_FALSE(NameMatchesHost("Router", "[::1]"));
}

TEST(TitleTidierTest, NameMatchesHostIgnoresCase) {
  EXPECT_TRUE(NameMatchesHost("GITHUB", "github.com"));
  EXPECT_TRUE(NameMatchesHost("git hub", "github.com"));
  EXPECT_FALSE(NameMatchesHost("", "github.com"));
  EXPECT_FALSE(NameMatchesHost("GitHub", ""));
}

TEST(TitleTidierTest, ALongTitleIsCutBeforeItIsRead) {
  std::string title;
  while (title.size() < 20000) {
    title += "word - ";
  }
  EXPECT_LE(TidyTitle(title, "example.org").size(), kMaxTidyTitleBytes);
}

TEST(TitleTidierTest, TheCutNeverSplitsAMultibyteCharacter) {
  std::string title(kMaxTidyTitleBytes - 1, 'a');
  title += "\xE2\x82\xAC\xE2\x82\xAC";  // Two euro signs across the limit.
  const std::string tidied = TidyTitle(title, "example.org");
  // Valid UTF-8: no lone continuation byte at the end.
  ASSERT_FALSE(tidied.empty());
  const unsigned char last = static_cast<unsigned char>(tidied.back());
  EXPECT_TRUE(last < 0x80 || last >= 0xC0 || tidied.size() >= 3);
  EXPECT_NE(tidied.find("a"), std::string::npos);
}

TEST(TitleTidierTest, CleanSeparatorsClosesTheGapOfARemovedText) {
  EXPECT_EQ(CleanSeparators("Foo - "), "Foo");
  EXPECT_EQ(CleanSeparators("Foo -  | Bar"), "Foo - Bar");
  EXPECT_EQ(CleanSeparators("  a   b "), "a b");
}

}  // namespace aerie_tab_model
