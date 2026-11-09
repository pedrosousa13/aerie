// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_WIDEVINE_WIDEVINE_OPT_IN_H_
#define COMPONENTS_AERIE_WIDEVINE_WIDEVINE_OPT_IN_H_

#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "url/gurl.h"

class PrefRegistrySimple;
class PrefService;

// Widevine is a Google component. Aerie builds it in but does not download or
// enable it until the user opts in, in Settings or in the one-time prompt on
// the first DRM request. Both prefs are in Local State: the component updater
// is one per browser, not one per profile.
namespace aerie_widevine {

// bool, default false. The opt-in.
inline constexpr char kWidevineEnabled[] = "aerie.widevine.enabled";
// List of host names whose DRM prompt the user declined. Never prompted again.
inline constexpr char kDeclinedSites[] = "aerie.widevine.declined_sites";

// The component updater's app id of the Widevine CDM (the extension id of its
// pinned signing key).
inline constexpr char kWidevineAppId[] = "oimompecagnajdejgnnjijobebaeigek";

void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

bool IsEnabled(const PrefService* local_state);
void SetEnabled(PrefService* local_state, bool enabled);

// True if the user declined the prompt for `host` in Local State or, only when
// `off_the_record`, earlier in this browser session in an off-the-record
// window. An off-the-record decline never applies to a regular window.
bool IsDeclined(const PrefService* local_state,
                std::string_view host,
                bool off_the_record);
// Remembers a decline. An off-the-record window must leave no trace on disk, so
// its declines (`off_the_record`) are kept in memory until the browser exits;
// any other decline is written to Local State.
void RecordDecline(PrefService* local_state,
                   std::string_view host,
                   bool off_the_record = false);
void ClearSessionDeclinesForTesting();

// True if the one-time prompt should be shown for a DRM request from `host`:
// Widevine is off, the host is not empty and the user did not decline it (see
// IsDeclined for `off_the_record`).
bool ShouldPrompt(const PrefService* local_state,
                  std::string_view host,
                  bool off_the_record);

// The component update server URL. While the user is opted in it is Google's
// (the only server that hosts the Widevine component); otherwise `fallback`,
// which is Helium's dummy origin that the network stack refuses.
GURL ComponentUpdateUrl(const PrefService* local_state, const GURL& fallback);

// Whether the component updater may name `app_id` in a request to `url`, the
// update or ping server it actually uses (update check or event ping). Only
// Google's update server (what ComponentUpdateUrl gives after the opt-in) is
// restricted, to the Widevine component: the CRLSet and every other registered
// component never reach Google. Any other URL, such as Helium's dummy origin
// that the network stack refuses, may be sent every app. This looks at the URL
// and not at the pref, because the URL is read when a check starts and the pref
// can change before the request is built.
bool ShouldSendToUpdateServer(std::string_view app_id, const GURL& url);

// Calls `register_widevine` once: now if the user is opted in, otherwise when
// the pref turns on. `register_widevine` registers the Widevine component with
// the component updater, which then downloads it. Never called while Widevine
// is off. Must not outlive `local_state`.
class RegistrationGate {
 public:
  RegistrationGate(PrefService* local_state,
                   base::RepeatingClosure register_widevine);
  RegistrationGate(const RegistrationGate&) = delete;
  RegistrationGate& operator=(const RegistrationGate&) = delete;
  ~RegistrationGate();

 private:
  void Update();

  raw_ptr<PrefService> local_state_;
  base::RepeatingClosure register_widevine_;
  PrefChangeRegistrar registrar_;
  bool registered_ = false;
};

// Creates a RegistrationGate for the browser process and never destroys it
// (its destructor would touch the pref service at shutdown, after it is gone).
void RegisterWhenEnabled(PrefService* local_state,
                         base::RepeatingClosure register_widevine);

}  // namespace aerie_widevine

#endif  // COMPONENTS_AERIE_WIDEVINE_WIDEVINE_OPT_IN_H_
