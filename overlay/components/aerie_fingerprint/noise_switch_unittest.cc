// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The per-site fingerprinting switch in an off-the-record profile (#114). The
// Bench cannot drive it: chrome://settings does not open in an off-the-record
// profile. These tests use the same pieces the switch uses: the site pattern
// and an off-the-record HostContentSettingsMap.

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "components/aerie_fingerprint/site_key.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_fingerprint {

namespace {

// The pref that holds the HELIUM_NOISE exceptions. Each content settings type
// has its own registered pref: the parent "profile.content_settings.exceptions"
// is a path, not a registered pref, and reading it is a CHECK failure. The type
// name "helium-noise" is stored with '_' in place of '-'.
constexpr char kHeliumNoisePref[] =
    "profile.content_settings.exceptions.helium_noise";

class NoiseSwitchTest : public testing::Test {
 protected:
  NoiseSwitchTest() {
    HostContentSettingsMap::RegisterProfilePrefs(prefs_.registry());
  }

  scoped_refptr<HostContentSettingsMap> MakeMap(bool off_the_record) {
    return base::MakeRefCounted<HostContentSettingsMap>(
        &prefs_, off_the_record, /*store_last_modified=*/false,
        /*restore_session=*/false, /*should_record_metrics=*/false);
  }

  ContentSetting Get(HostContentSettingsMap* map, const char* url) {
    return map->GetContentSetting(GURL(url), GURL(),
                                  ContentSettingsType::HELIUM_NOISE);
  }

  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(NoiseSwitchTest, ASwitchSetOffTheRecordCoversTheSiteAndStaysInMemory) {
  auto regular = MakeMap(/*off_the_record=*/false);
  auto otr = MakeMap(/*off_the_record=*/true);

  otr->SetContentSettingCustomScope(
      NoiseSitePattern(GURL("https://x.example.com/")),
      ContentSettingsPattern::Wildcard(), ContentSettingsType::HELIUM_NOISE,
      CONTENT_SETTING_BLOCK);

  EXPECT_EQ(CONTENT_SETTING_BLOCK, Get(otr.get(), "https://x.example.com/"));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, Get(otr.get(), "https://y.example.com/"));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, Get(otr.get(), "https://example.com:8443/"));
  // Another scheme and another site keep the noise.
  EXPECT_EQ(CONTENT_SETTING_ALLOW, Get(otr.get(), "http://example.com/"));
  EXPECT_EQ(CONTENT_SETTING_ALLOW, Get(otr.get(), "https://example.org/"));

  // The regular profile never sees it, and nothing reached the preferences.
  EXPECT_EQ(CONTENT_SETTING_ALLOW, Get(regular.get(), "https://x.example.com/"));
  EXPECT_TRUE(prefs_.GetDict(kHeliumNoisePref).empty());

  // A later off-the-record map (a new window) starts without it.
  auto otr_later = MakeMap(/*off_the_record=*/true);
  EXPECT_EQ(CONTENT_SETTING_ALLOW, Get(otr_later.get(), "https://x.example.com/"));

  otr_later->ShutdownOnUIThread();
  otr->ShutdownOnUIThread();
  regular->ShutdownOnUIThread();
}

TEST_F(NoiseSwitchTest, ARegularSwitchIsStoredUnderTheHeliumNoisePrefKey) {
  auto regular = MakeMap(/*off_the_record=*/false);
  regular->SetContentSettingCustomScope(
      NoiseSitePattern(GURL("http://x.a.localhost:4100/")),
      ContentSettingsPattern::Wildcard(), ContentSettingsType::HELIUM_NOISE,
      CONTENT_SETTING_BLOCK);

  EXPECT_TRUE(
      prefs_.GetDict(kHeliumNoisePref).FindDict("http://[*.]a.localhost,*"));
  regular->ShutdownOnUIThread();
}

}  // namespace

}  // namespace aerie_fingerprint
