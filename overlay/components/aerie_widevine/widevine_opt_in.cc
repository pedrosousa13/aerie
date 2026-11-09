// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_widevine/widevine_opt_in.h"

#include <functional>
#include <set>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace aerie_widevine {

namespace {

// Omaha, the update server for Chromium components. It hosts the Widevine CRX
// and nothing else in Aerie asks it for anything: without the opt-in the
// component updater is given Helium's dummy origin.
constexpr char kComponentUpdateUrl[] =
    "https://update.googleapis.com/service/update2/json";

// Hosts declined in off-the-record windows. Browser UI thread only.
std::set<std::string, std::less<>>& SessionDeclines() {
  static base::NoDestructor<std::set<std::string, std::less<>>> declines;
  return *declines;
}

bool IsDeclinedInLocalState(const PrefService* local_state,
                            std::string_view host) {
  if (!local_state) {
    return false;
  }
  for (const base::Value& entry : local_state->GetList(kDeclinedSites)) {
    if (entry.is_string() && entry.GetString() == host) {
      return true;
    }
  }
  return false;
}

}  // namespace

RegistrationGate::RegistrationGate(PrefService* local_state,
                                   base::RepeatingClosure register_widevine)
    : local_state_(local_state),
      register_widevine_(std::move(register_widevine)) {
  registrar_.Init(local_state);
  registrar_.Add(kWidevineEnabled,
                 base::BindRepeating(&RegistrationGate::Update,
                                     base::Unretained(this)));
  Update();
}

RegistrationGate::~RegistrationGate() = default;

void RegistrationGate::Update() {
  if (registered_ || !IsEnabled(local_state_)) {
    return;
  }
  registered_ = true;
  register_widevine_.Run();
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kWidevineEnabled, false);
  registry->RegisterListPref(kDeclinedSites);
}

bool IsEnabled(const PrefService* local_state) {
  return local_state && local_state->GetBoolean(kWidevineEnabled);
}

void SetEnabled(PrefService* local_state, bool enabled) {
  local_state->SetBoolean(kWidevineEnabled, enabled);
}

bool IsDeclined(const PrefService* local_state,
                std::string_view host,
                bool off_the_record) {
  if (host.empty()) {
    return false;
  }
  return (off_the_record && SessionDeclines().contains(host)) ||
         IsDeclinedInLocalState(local_state, host);
}

void ClearSessionDeclinesForTesting() {
  SessionDeclines().clear();
}

void RecordDecline(PrefService* local_state,
                   std::string_view host,
                   bool off_the_record) {
  if (host.empty()) {
    return;
  }
  if (off_the_record) {
    SessionDeclines().emplace(host);
    return;
  }
  if (IsDeclinedInLocalState(local_state, host)) {
    return;
  }
  ScopedListPrefUpdate update(local_state, kDeclinedSites);
  update->Append(std::string(host));
}

bool ShouldPrompt(const PrefService* local_state,
                  std::string_view host,
                  bool off_the_record) {
  return !IsEnabled(local_state) && !host.empty() &&
         !IsDeclined(local_state, host, off_the_record);
}

GURL ComponentUpdateUrl(const PrefService* local_state, const GURL& fallback) {
  return IsEnabled(local_state) ? GURL(kComponentUpdateUrl) : fallback;
}

bool ShouldSendToUpdateServer(std::string_view app_id, const GURL& url) {
  const GURL google(kComponentUpdateUrl);
  if (!url.SchemeIs(google.GetScheme()) || url.host() != google.host()) {
    return true;
  }
  return app_id == kWidevineAppId;
}

void RegisterWhenEnabled(PrefService* local_state,
                         base::RepeatingClosure register_widevine) {
  if (!local_state) {
    return;
  }
  // One gate for the browser process. A second call is a bug in the caller:
  // it trips a DCHECK, and in a release build it is ignored.
  static bool called = false;
  DCHECK(!called) << "aerie_widevine::RegisterWhenEnabled called twice";
  if (called) {
    return;
  }
  called = true;
  static base::NoDestructor<RegistrationGate> gate(
      local_state, std::move(register_widevine));
}

}  // namespace aerie_widevine
