// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/import_sources.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/base_paths.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_path_override.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_import_sources {

namespace {

user_data_importer::SourceProfile Source(
    const std::u16string& name,
    user_data_importer::ImporterType type,
    const base::FilePath& path,
    const std::u16string& profile = u"") {
  user_data_importer::SourceProfile source;
  source.importer_name = name;
  source.importer_type = type;
  source.source_path = path;
  source.profile = profile;
  return source;
}

std::vector<std::u16string> Names(
    const std::vector<user_data_importer::SourceProfile>& profiles) {
  std::vector<std::u16string> names;
  for (const auto& p : profiles) {
    names.push_back(p.importer_name);
  }
  return names;
}

}  // namespace

class NormalizeSourcesTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    // Resolve symlinks in the temp dir itself.
    home_ = base::MakeAbsoluteFilePath(dir_.GetPath());
    ASSERT_FALSE(home_.empty());
  }
  base::FilePath Dir(const std::string& relative) {
    const base::FilePath path = home_.AppendASCII(relative);
    EXPECT_TRUE(base::CreateDirectory(path));
    return path;
  }

  base::ScopedTempDir dir_;
  base::FilePath home_;
};

TEST_F(NormalizeSourcesTest,
       ANativeAndAFlatpakInstallAreBothListedAndToldApart) {
  const auto native = Dir(".config/chromium/Default");
  const auto flatpak =
      Dir(".var/app/org.chromium.Chromium/config/chromium/Default");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium", user_data_importer::TYPE_CHROME, native),
      Source(u"Chromium (Flatpak)", user_data_importer::TYPE_CHROME, flatpak),
  };
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(Names(profiles),
            (std::vector<std::u16string>{u"Chromium", u"Chromium (Flatpak)"}));
}

TEST_F(NormalizeSourcesTest, AFlatpakSourceWithoutTheLabelGetsIt) {
  const auto zen = Dir(".var/app/app.zen_browser.zen/.zen/abc.Default");
  const auto ff =
      Dir(".var/app/org.mozilla.firefox/.mozilla/firefox/x.default");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Zen", user_data_importer::TYPE_ZEN, zen),
      Source(u"Firefox", user_data_importer::TYPE_FIREFOX, ff),
  };
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(Names(profiles), (std::vector<std::u16string>{
                                 u"Zen (Flatpak)", u"Firefox (Flatpak)"}));
}

TEST_F(NormalizeSourcesTest, TheLabelIsNotAddedTwice) {
  const auto flatpak = Dir(".var/app/org.chromium.Chromium/config/chromium");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium (Flatpak)", user_data_importer::TYPE_CHROME, flatpak)};
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(Names(profiles),
            (std::vector<std::u16string>{u"Chromium (Flatpak)"}));
}

TEST_F(NormalizeSourcesTest, NativeSourcesAreLeftAlone) {
  const auto zen = Dir(".zen/abc.Default");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Zen", user_data_importer::TYPE_ZEN, zen)};
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(Names(profiles), (std::vector<std::u16string>{u"Zen"}));
}

TEST_F(NormalizeSourcesTest, ASourceListedTwiceIsListedOnce) {
  const auto path = Dir(".config/chromium/Default");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium", user_data_importer::TYPE_CHROME, path),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, path),
  };
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(profiles.size(), 1u);
}

TEST_F(NormalizeSourcesTest, TwoPathsToTheSameFolderAreListedOnce) {
  // Zen's second Flatpak id (io.github.zen_browser.zen) is a symlink to the
  // first.
  const auto real = Dir(".var/app/app.zen_browser.zen/.zen/abc.Default");
  ASSERT_TRUE(base::CreateSymbolicLink(
      base::FilePath("app.zen_browser.zen"),
      home_.AppendASCII(".var/app/io.github.zen_browser.zen")));
  const auto alias = home_.AppendASCII(
      ".var/app/io.github.zen_browser.zen/.zen/abc.Default");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Zen", user_data_importer::TYPE_ZEN, real),
      Source(u"Zen", user_data_importer::TYPE_ZEN, alias),
  };
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(Names(profiles), (std::vector<std::u16string>{u"Zen (Flatpak)"}));
  EXPECT_EQ(profiles[0].source_path, real);
}

TEST_F(NormalizeSourcesTest, DifferentFoldersOrTypesAreKeptAndOrderIsKept) {
  const auto a = Dir(".config/chromium/Default");
  const auto b = Dir(".config/chromium/Profile 1");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium", user_data_importer::TYPE_CHROME, a, u"One"),
      Source(u"Brave", user_data_importer::TYPE_BRAVE, a, u"Other type"),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, b, u"Two"),
  };
  NormalizeSources(home_, &profiles);
  ASSERT_EQ(profiles.size(), 3u);
  EXPECT_EQ(profiles[0].profile, u"One");
  EXPECT_EQ(profiles[1].profile, u"Other type");
  EXPECT_EQ(profiles[2].profile, u"Two");
}

TEST_F(NormalizeSourcesTest, TwoProfilesInOneFolderAreTwoRows) {
  // Upstream gives each profile its own folder (<user data>/Default, <user data>/Profile 1), so this
  // does not happen today; the key still has to be the profile, not only the folder.
  const auto folder = Dir(".config/chromium/Default");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium", user_data_importer::TYPE_CHROME, folder, u"Work"),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, folder, u"Home"),
  };
  NormalizeSources(home_, &profiles);
  ASSERT_EQ(profiles.size(), 2u);
  EXPECT_EQ(profiles[0].profile, u"Work");
  EXPECT_EQ(profiles[1].profile, u"Home");
}

TEST_F(NormalizeSourcesTest, ASymlinkedCopyOfAFolderKeepsOneRowPerProfile) {
  const auto real = Dir(".config/chromium");
  ASSERT_TRUE(base::CreateDirectory(real.AppendASCII("Default")));
  ASSERT_TRUE(base::CreateDirectory(real.AppendASCII("Profile 1")));
  ASSERT_TRUE(base::CreateSymbolicLink(real, home_.AppendASCII("chromium-link")));
  const auto link = home_.AppendASCII("chromium-link");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium", user_data_importer::TYPE_CHROME, real.AppendASCII("Default"), u"One"),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, real.AppendASCII("Profile 1"), u"Two"),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, link.AppendASCII("Default"), u"One"),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, link.AppendASCII("Profile 1"), u"Two"),
  };
  NormalizeSources(home_, &profiles);
  ASSERT_EQ(profiles.size(), 2u);
  EXPECT_EQ(profiles[0].profile, u"One");
  EXPECT_EQ(profiles[1].profile, u"Two");
}

TEST_F(NormalizeSourcesTest, RowsWithoutAFolderAreKept) {
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Bookmarks HTML file", user_data_importer::TYPE_BOOKMARKS_FILE,
             base::FilePath()),
      Source(u"Bookmarks HTML file", user_data_importer::TYPE_BOOKMARKS_FILE,
             base::FilePath()),
  };
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(profiles.size(), 2u);
}

TEST_F(NormalizeSourcesTest, FoldersThatDoNotExistStillCompareByPath) {
  const base::FilePath gone = home_.AppendASCII(".config/chromium/Gone");
  std::vector<user_data_importer::SourceProfile> profiles = {
      Source(u"Chromium", user_data_importer::TYPE_CHROME, gone),
      Source(u"Chromium", user_data_importer::TYPE_CHROME, gone),
  };
  NormalizeSources(home_, &profiles);
  EXPECT_EQ(profiles.size(), 1u);
}

TEST(ExtraChromiumSourcesTest, FlatpakChromiumFamilyAndHelium) {
  const base::FilePath home("/h");
  const auto sources = ExtraChromiumSources(home);
  auto find = [&](const std::string& brand) -> const ExtraChromiumSource* {
    for (const auto& s : sources) {
      if (s.brand == brand) {
        return &s;
      }
    }
    return nullptr;
  };
  const auto* chromium = find("Chromium (Flatpak)");
  ASSERT_TRUE(chromium);
  EXPECT_EQ(chromium->user_data_folder,
            base::FilePath("/h/.var/app/org.chromium.Chromium/config/chromium"));
  EXPECT_EQ(chromium->importer_type, user_data_importer::TYPE_CHROME);

  const auto* brave = find("Brave (Flatpak)");
  ASSERT_TRUE(brave);
  EXPECT_EQ(brave->user_data_folder,
            base::FilePath("/h/.var/app/com.brave.Browser/config/"
                           "BraveSoftware/Brave-Browser"));
  EXPECT_EQ(brave->importer_type, user_data_importer::TYPE_BRAVE);

  EXPECT_TRUE(find("Google Chrome (Flatpak)"));
  EXPECT_TRUE(find("Microsoft Edge (Flatpak)"));
  EXPECT_TRUE(find("Vivaldi (Flatpak)"));
  EXPECT_TRUE(find("Opera (Flatpak)"));

  const auto* helium = find("Helium");
  ASSERT_TRUE(helium);
  // Listed once: the Helium entry of the browser table is the only place that says where it lives.
  EXPECT_EQ(std::count_if(sources.begin(), sources.end(),
                          [](const auto& s) { return s.brand == "Helium"; }),
            1);
  EXPECT_EQ(helium->user_data_folder,
            base::FilePath("/h/.config/net.imput.helium"));
  EXPECT_EQ(helium->importer_type, user_data_importer::TYPE_CHROME);
}

TEST(ExtraChromiumSourcesTest,
     FirefoxAndZenAreNotAddedBecauseTheImporterFindsThem) {
  for (const auto& s : ExtraChromiumSources(base::FilePath("/h"))) {
    EXPECT_EQ(s.brand.find("Firefox"), std::string::npos) << s.brand;
    EXPECT_EQ(s.brand.find("Zen"), std::string::npos) << s.brand;
  }
}

TEST(HomeDirectoryTest, IsTheHomeDirectoryOfThePathService) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::ScopedPathOverride home(base::DIR_HOME, dir.GetPath());
  EXPECT_EQ(HomeDirectory(), dir.GetPath());
}

TEST(ExtraChromiumSourcesTest, EveryFlatpakBrandEndsInTheLabel) {
  for (const auto& s : ExtraChromiumSources(base::FilePath("/h"))) {
    if (s.brand != "Helium") {
      EXPECT_TRUE(s.brand.ends_with(kFlatpakSuffix)) << s.brand;
    }
  }
}

}  // namespace aerie_import_sources
