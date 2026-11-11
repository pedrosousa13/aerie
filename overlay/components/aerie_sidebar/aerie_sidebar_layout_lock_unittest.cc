// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_layout_lock.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/prefs/testing_pref_store.h"
#include "components/prefs/value_map_pref_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

// A real PrefService with the layers Chrome builds: a user store that an old
// profile or Settings could have filled, and the command-line layer that
// LockHeliumLayout fills. Helium's two prefs are registered as Helium does.
class AerieSidebarLayoutLockTest : public testing::Test {
 public:
  AerieSidebarLayoutLockTest() {
    user_store_ = base::MakeRefCounted<TestingPrefStore>();
    command_line_store_ = base::MakeRefCounted<ValueMapPrefStore>();
    LockHeliumLayout(command_line_store_.get());

    auto registry = base::MakeRefCounted<PrefRegistrySimple>();
    registry->RegisterIntegerPref(kHeliumLayoutPref, 0);  // Classic.
    registry->RegisterBooleanPref(kHeliumZenModePref, false);
    PrefServiceFactory factory;
    factory.set_user_prefs(user_store_);
    factory.set_command_line_prefs(command_line_store_);
    prefs_ = factory.Create(registry);
  }

 protected:
  scoped_refptr<TestingPrefStore> user_store_;
  scoped_refptr<ValueMapPrefStore> command_line_store_;
  std::unique_ptr<PrefService> prefs_;
};

TEST_F(AerieSidebarLayoutLockTest, HeliumReadsVerticalWithZenModeOff) {
  EXPECT_EQ(prefs_->GetInteger(kHeliumLayoutPref), kHeliumLayoutVertical);
  EXPECT_FALSE(prefs_->GetBoolean(kHeliumZenModePref));
}

TEST_F(AerieSidebarLayoutLockTest, TheUserCannotModifyEitherPref) {
  EXPECT_FALSE(prefs_->IsUserModifiablePreference(kHeliumLayoutPref));
  EXPECT_FALSE(prefs_->IsUserModifiablePreference(kHeliumZenModePref));
}

TEST_F(AerieSidebarLayoutLockTest, AWriteDoesNotChangeWhatHeliumReads) {
  prefs_->SetInteger(kHeliumLayoutPref, 3);  // Dynamic.
  prefs_->SetBoolean(kHeliumZenModePref, true);
  EXPECT_EQ(prefs_->GetInteger(kHeliumLayoutPref), kHeliumLayoutVertical);
  EXPECT_FALSE(prefs_->GetBoolean(kHeliumZenModePref));
}

TEST_F(AerieSidebarLayoutLockTest, ObserversOnlyEverReadTheLockedValues) {
  // PrefService may still tell observers that the user layer changed; what they
  // read is the locked value, so nothing has to write it back.
  std::vector<int> layouts;
  std::vector<bool> zens;
  PrefChangeRegistrar registrar;
  registrar.Init(prefs_.get());
  registrar.Add(kHeliumLayoutPref,
                base::BindRepeating(
                    [](std::vector<int>* out, PrefService* prefs) {
                      out->push_back(prefs->GetInteger(kHeliumLayoutPref));
                    },
                    base::Unretained(&layouts), base::Unretained(prefs_.get())));
  registrar.Add(kHeliumZenModePref,
                base::BindRepeating(
                    [](std::vector<bool>* out, PrefService* prefs) {
                      out->push_back(prefs->GetBoolean(kHeliumZenModePref));
                    },
                    base::Unretained(&zens), base::Unretained(prefs_.get())));
  prefs_->SetInteger(kHeliumLayoutPref, 1);
  prefs_->SetBoolean(kHeliumZenModePref, true);
  prefs_->ClearPref(kHeliumLayoutPref);
  for (int layout : layouts) {
    EXPECT_EQ(layout, kHeliumLayoutVertical);
  }
  for (bool zen : zens) {
    EXPECT_FALSE(zen);
  }
}

TEST_F(AerieSidebarLayoutLockTest, AnOldProfileWithAnotherLayoutIsHeldToo) {
  // The values an old profile file holds are under the lock from the start.
  auto old_user_store = base::MakeRefCounted<TestingPrefStore>();
  old_user_store->SetInteger(kHeliumLayoutPref, 1);
  old_user_store->SetBoolean(kHeliumZenModePref, true);
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  registry->RegisterIntegerPref(kHeliumLayoutPref, 0);
  registry->RegisterBooleanPref(kHeliumZenModePref, false);
  PrefServiceFactory factory;
  factory.set_user_prefs(old_user_store);
  factory.set_command_line_prefs(command_line_store_);
  std::unique_ptr<PrefService> prefs = factory.Create(registry);
  EXPECT_EQ(prefs->GetInteger(kHeliumLayoutPref), kHeliumLayoutVertical);
  EXPECT_FALSE(prefs->GetBoolean(kHeliumZenModePref));
  EXPECT_FALSE(prefs->IsUserModifiablePreference(kHeliumLayoutPref));
}

TEST_F(AerieSidebarLayoutLockTest, OtherPrefsStayUserModifiable) {
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  registry->RegisterIntegerPref("helium.browser.other", 0);
  registry->RegisterIntegerPref(kHeliumLayoutPref, 0);
  registry->RegisterBooleanPref(kHeliumZenModePref, false);
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<TestingPrefStore>());
  factory.set_command_line_prefs(command_line_store_);
  std::unique_ptr<PrefService> prefs = factory.Create(registry);
  EXPECT_TRUE(prefs->IsUserModifiablePreference("helium.browser.other"));
}

}  // namespace
}  // namespace aerie_sidebar
