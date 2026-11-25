// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/browser_icon.h"

#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/scoped_environment_variable_override.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_import_sources {

namespace {

// The 8 bytes every PNG starts with, then an IHDR chunk header, then filler.
std::string FakePng(const std::string& tag, size_t padding = 0) {
  return std::string("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16) + tag +
         std::string(padding, 'x');
}

constexpr char kDataUrlPrefix[] = "data:image/png;base64,";

}  // namespace

class BrowserIconTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    share_ = dir_.GetPath().AppendASCII("share");
    flatpak_ = dir_.GetPath().AppendASCII("flatpak/exports/share");
  }

  void Write(const base::FilePath& path, const std::string& bytes) {
    ASSERT_TRUE(base::CreateDirectory(path.DirName()));
    ASSERT_TRUE(base::WriteFile(path, bytes));
  }
  void WriteDesktop(const base::FilePath& root,
                    const std::string& id,
                    const std::string& icon_line) {
    Write(root.AppendASCII("applications").AppendASCII(id + ".desktop"),
          "[Desktop Entry]\nName=Test\nType=Application\n" + icon_line +
              "\nExec=test\n");
  }
  void WriteThemeIcon(const base::FilePath& root,
                      const std::string& theme,
                      const std::string& size,
                      const std::string& name,
                      const std::string& bytes) {
    Write(root.AppendASCII("icons")
              .AppendASCII(theme)
              .AppendASCII(size)
              .AppendASCII("apps")
              .AppendASCII(name + ".png"),
          bytes);
  }
  // The decoded bytes of a data URL, or "<none>".
  std::string Decoded(const std::string& url) {
    if (url.rfind(kDataUrlPrefix, 0) != 0) {
      return "<none>";
    }
    std::string out;
    if (!base::Base64Decode(url.substr(sizeof(kDataUrlPrefix) - 1), &out)) {
      return "<bad base64>";
    }
    return out;
  }

  base::ScopedTempDir dir_;
  base::FilePath share_;
  base::FilePath flatpak_;
};

TEST_F(BrowserIconTest, ResolvesTheIconNameThroughHicolor) {
  WriteDesktop(share_, "chromium", "Icon=chromium");
  WriteThemeIcon(share_, "hicolor", "48x48", "chromium", FakePng("a"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"chromium"})), FakePng("a"));
}

TEST_F(BrowserIconTest, PrefersTheSizeNearestToSixtyFour) {
  WriteDesktop(share_, "chromium", "Icon=chromium");
  WriteThemeIcon(share_, "hicolor", "16x16", "chromium", FakePng("16"));
  WriteThemeIcon(share_, "hicolor", "128x128", "chromium", FakePng("128"));
  WriteThemeIcon(share_, "hicolor", "64x64", "chromium", FakePng("64"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"chromium"})), FakePng("64"));
}

TEST_F(BrowserIconTest, FindsAFlatpakExportedIconAndDesktopEntry) {
  WriteDesktop(flatpak_, "org.chromium.Chromium", "Icon=org.chromium.Chromium");
  WriteThemeIcon(flatpak_, "hicolor", "128x128", "org.chromium.Chromium",
                 FakePng("flat"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_, flatpak_},
                                       {"org.chromium.Chromium"})),
            FakePng("flat"));
}

TEST_F(BrowserIconTest, TheIconMayLiveInAnotherDataDirThanTheDesktopEntry) {
  WriteDesktop(share_, "brave-browser", "Icon=brave-browser");
  WriteThemeIcon(flatpak_, "hicolor", "256x256", "brave-browser", FakePng("b"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_, flatpak_}, {"brave-browser"})),
            FakePng("b"));
}

TEST_F(BrowserIconTest, FallsBackToOtherThemesAndPixmaps) {
  WriteDesktop(share_, "vivaldi-stable", "Icon=vivaldi");
  WriteThemeIcon(share_, "Adwaita", "48x48", "vivaldi", FakePng("adw"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"vivaldi-stable"})),
            FakePng("adw"));

  WriteDesktop(share_, "opera", "Icon=opera");
  Write(share_.AppendASCII("pixmaps/opera.png"), FakePng("pix"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"opera"})), FakePng("pix"));
}

TEST_F(BrowserIconTest, TakesAnAbsoluteIconPath) {
  const base::FilePath icon = dir_.GetPath().AppendASCII("elsewhere/zen.png");
  Write(icon, FakePng("abs"));
  WriteDesktop(share_, "zen", "Icon=" + icon.value());
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"zen"})), FakePng("abs"));
}

TEST_F(BrowserIconTest, ReadsTheIconOfTheEntryGroupOnly) {
  Write(share_.AppendASCII("applications/firefox.desktop"),
        "[Desktop Entry]\nName=Firefox\n\n[Desktop Action new-window]\n"
        "Icon=wrong\n");
  WriteThemeIcon(share_, "hicolor", "48x48", "wrong", FakePng("wrong"));
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"firefox"}), "");
}

TEST_F(BrowserIconTest, TriesTheNextDesktopIdWhenTheFirstHasNoEntry) {
  WriteDesktop(share_, "chromium-browser", "Icon=chromium-browser");
  WriteThemeIcon(share_, "hicolor", "48x48", "chromium-browser", FakePng("cb"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_},
                                       {"chromium", "chromium-browser"})),
            FakePng("cb"));
}

TEST_F(BrowserIconTest, NothingWhenThereIsNoEntryOrNoIconFile) {
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"chromium"}), "");
  WriteDesktop(share_, "chromium", "Icon=chromium");
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"chromium"}), "");
  EXPECT_EQ(BrowserIconDataUrl({share_}, {}), "");
}

TEST_F(BrowserIconTest, NothingWhenTheFileIsNotAPng) {
  WriteDesktop(share_, "chromium", "Icon=chromium");
  WriteThemeIcon(share_, "hicolor", "48x48", "chromium",
                 "<svg xmlns='http://www.w3.org/2000/svg'/>");
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"chromium"}), "");
  // A GIF with a .png name is not a PNG either.
  WriteThemeIcon(share_, "hicolor", "64x64", "chromium", "GIF89a\x01\x00");
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"chromium"}), "");
}

TEST_F(BrowserIconTest, SkipsAFileOverTheCapAndUsesTheNextSize) {
  WriteDesktop(share_, "chromium", "Icon=chromium");
  WriteThemeIcon(share_, "hicolor", "64x64", "chromium",
                 FakePng("big", kMaxIconBytes));
  WriteThemeIcon(share_, "hicolor", "48x48", "chromium", FakePng("small"));
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"chromium"})),
            FakePng("small"));
}

TEST_F(BrowserIconTest, AFileJustUnderTheCapIsKept) {
  WriteDesktop(share_, "chromium", "Icon=chromium");
  const std::string png = FakePng("", kMaxIconBytes - 16);
  ASSERT_EQ(png.size(), kMaxIconBytes);
  WriteThemeIcon(share_, "hicolor", "64x64", "chromium", png);
  EXPECT_EQ(Decoded(BrowserIconDataUrl({share_}, {"chromium"})), png);
}

TEST_F(BrowserIconTest, RefusesIconNamesThatLeaveTheIconDirectories) {
  Write(dir_.GetPath().AppendASCII("secret.png"), FakePng("secret"));
  WriteDesktop(share_, "chromium", "Icon=../../../secret");
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"chromium"}), "");
}

TEST_F(BrowserIconTest, RefusesDesktopIdsThatLeaveTheApplicationsDirectory) {
  Write(dir_.GetPath().AppendASCII("evil.desktop"),
        "[Desktop Entry]\nIcon=chromium\n");
  WriteThemeIcon(share_, "hicolor", "48x48", "chromium", FakePng("a"));
  EXPECT_EQ(BrowserIconDataUrl({share_}, {"../evil"}), "");
}

TEST_F(BrowserIconTest, SystemDataDirsFollowTheXdgVariablesThenFlatpak) {
  base::ScopedEnvironmentVariableOverride home("HOME", "/home/u");
  base::ScopedEnvironmentVariableOverride data_home("XDG_DATA_HOME", "/xh");
  base::ScopedEnvironmentVariableOverride data_dirs("XDG_DATA_DIRS",
                                                    "/a/share:/b/share");
  EXPECT_EQ(SystemDataDirs(),
            (std::vector<base::FilePath>{
                base::FilePath("/xh"), base::FilePath("/a/share"),
                base::FilePath("/b/share"),
                base::FilePath("/home/u/.local/share/flatpak/exports/share"),
                base::FilePath("/var/lib/flatpak/exports/share")}));
}

TEST_F(BrowserIconTest, SystemDataDirsDefaultsWhenTheVariablesAreUnset) {
  base::ScopedEnvironmentVariableOverride home("HOME", "/home/u");
  base::ScopedEnvironmentVariableOverride data_home("XDG_DATA_HOME");
  base::ScopedEnvironmentVariableOverride data_dirs("XDG_DATA_DIRS");
  EXPECT_EQ(SystemDataDirs(),
            (std::vector<base::FilePath>{
                base::FilePath("/home/u/.local/share"),
                base::FilePath("/usr/local/share"),
                base::FilePath("/usr/share"),
                base::FilePath("/home/u/.local/share/flatpak/exports/share"),
                base::FilePath("/var/lib/flatpak/exports/share")}));
}

TEST(DesktopIdsForBrowserTest, NativeNamesGetNativeIds) {
  EXPECT_EQ(DesktopIdsForBrowser("Chromium"),
            (std::vector<std::string>{"chromium", "chromium-browser"}));
  EXPECT_EQ(DesktopIdsForBrowser("Brave").front(), "brave-browser");
  EXPECT_EQ(DesktopIdsForBrowser("Google Chrome").front(), "google-chrome");
  EXPECT_EQ(DesktopIdsForBrowser("Microsoft Edge").front(), "microsoft-edge");
  EXPECT_EQ(DesktopIdsForBrowser("Vivaldi").front(), "vivaldi-stable");
  EXPECT_EQ(DesktopIdsForBrowser("Firefox").front(), "firefox");
  EXPECT_EQ(DesktopIdsForBrowser("Zen").front(), "zen");
  EXPECT_FALSE(DesktopIdsForBrowser("Helium").empty());
}

TEST(DesktopIdsForBrowserTest, FirefoxIsNamedMozillaFirefoxByTheImporter) {
  // GetFirefoxImporterName() answers "Mozilla Firefox" when it cannot read application.ini.
  EXPECT_EQ(DesktopIdsForBrowser("Mozilla Firefox"),
            DesktopIdsForBrowser("Firefox"));
  EXPECT_EQ(DesktopIdsForBrowser("Mozilla Firefox (Flatpak)"),
            (std::vector<std::string>{"org.mozilla.firefox"}));
}

TEST(DesktopIdsForBrowserTest, FlatpakNamesGetFlatpakIdsOnly) {
  EXPECT_EQ(DesktopIdsForBrowser("Chromium (Flatpak)"),
            (std::vector<std::string>{"org.chromium.Chromium"}));
  EXPECT_EQ(DesktopIdsForBrowser("Firefox (Flatpak)"),
            (std::vector<std::string>{"org.mozilla.firefox"}));
  EXPECT_EQ(DesktopIdsForBrowser("Zen (Flatpak)"),
            (std::vector<std::string>{"app.zen_browser.zen"}));
}

TEST(DesktopIdsForBrowserTest, UnknownNamesGetNothing) {
  EXPECT_TRUE(DesktopIdsForBrowser("").empty());
  EXPECT_TRUE(DesktopIdsForBrowser("../../etc/passwd").empty());
  EXPECT_TRUE(DesktopIdsForBrowser("Bookmarks HTML file").empty());
  EXPECT_TRUE(DesktopIdsForBrowser("Nope (Flatpak)").empty());
}

}  // namespace aerie_import_sources
