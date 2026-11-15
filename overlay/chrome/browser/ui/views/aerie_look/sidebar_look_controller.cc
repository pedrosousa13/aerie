// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_look/sidebar_look_controller.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/color/aerie/aerie_color_mixer.h"
#include "chrome/browser/ui/views/aerie_look/aerie_look.h"
#include "chrome/common/pref_names.h"
#include "components/aerie_design_tokens/aerie_appearance_prefs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie::tokens::ModeChoice;

ThemeService::BrowserColorScheme SchemeOf(ModeChoice choice) {
  switch (choice) {
    case ModeChoice::kLight:
      return ThemeService::BrowserColorScheme::kLight;
    case ModeChoice::kDark:
      return ThemeService::BrowserColorScheme::kDark;
    case ModeChoice::kSystem:
      break;
  }
  return ThemeService::BrowserColorScheme::kSystem;
}

ModeChoice ChoiceOf(ThemeService::BrowserColorScheme scheme) {
  switch (scheme) {
    case ThemeService::BrowserColorScheme::kLight:
      return ModeChoice::kLight;
    case ThemeService::BrowserColorScheme::kDark:
      return ModeChoice::kDark;
    case ThemeService::BrowserColorScheme::kSystem:
      break;
  }
  return ModeChoice::kSystem;
}

ModeChoice ModePref(const PrefService& prefs) {
  return aerie::appearance::ParseModeChoice(
             prefs.GetString(aerie::appearance::kModePref))
      .value_or(ModeChoice::kSystem);
}

}  // namespace

SidebarLookController::SidebarLookController(Profile* profile,
                                             aerie_tab_model::TabModel* model,
                                             Client* client)
    : profile_(profile),
      model_(model),
      client_(client),
      crossfade_(profile->GetPrefs()) {
  PrefService* prefs = profile_->GetPrefs();
  ThemeService* themes = ThemeServiceFactory::GetForProfile(profile_);

  // The first window of the session settles which of the two holds the Mode:
  // the Aerie pref when the user has set it, else the browser's colour scheme
  // (a person who chose Dark before Aerie keeps it). Nothing is animated, and
  // it is done before the first colour provider of the window is built.
  if (themes) {
    if (prefs->GetUserPrefValue(aerie::appearance::kModePref)) {
      SyncBrowserColorSchemeFromMode();
    } else if (themes->GetBrowserColorScheme() !=
               ThemeService::BrowserColorScheme::kSystem) {
      prefs->SetString(aerie::appearance::kModePref,
                       aerie::appearance::ModeChoiceName(
                           ChoiceOf(themes->GetBrowserColorScheme())));
    }
  }

  pref_registrar_.Init(prefs);
  const auto on_appearance = base::BindRepeating(
      &SidebarLookController::OnAppearancePrefChanged, base::Unretained(this));
  pref_registrar_.Add(aerie::appearance::kThemePref, on_appearance);
  pref_registrar_.Add(aerie::appearance::kModePref, on_appearance);
  pref_registrar_.Add(
      prefs::kBrowserColorScheme,
      base::BindRepeating(&SidebarLookController::OnBrowserColorSchemeChanged,
                          base::Unretained(this)));

  native_theme_observations_.AddObservation(
      ui::NativeTheme::GetInstanceForNativeUi());
  // The client is still being made: it reads the look when it is ready.
  Apply(/*notify_client=*/false);
}

SidebarLookController::~SidebarLookController() = default;

const ui::NativeTheme& SidebarLookController::native_theme() const {
  views::Widget* widget = client_->GetLookWidget();
  return widget ? *widget->GetNativeTheme()
                : *ui::NativeTheme::GetInstanceForNativeUi();
}

aerie::tokens::EffectiveLook SidebarLookController::look() const {
  return aerie_look::ComputeEffectiveLook(
      *profile_->GetPrefs(), model_ ? &model_->workspace() : nullptr,
      native_theme());
}

void SidebarLookController::OnWorkspaceChanged() {
  Apply();
}

void SidebarLookController::OnAddedToWidget() {
  views::Widget* widget = client_->GetLookWidget();
  if (widget && !native_theme_observations_.IsObservingSource(
                    widget->GetNativeTheme())) {
    native_theme_observations_.AddObservation(widget->GetNativeTheme());
  }
  Apply();
}

void SidebarLookController::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  Apply();
}

void SidebarLookController::OnAppearancePrefChanged() {
  // The snapshot is taken first, while the window still has the old look. The
  // views paint in the next frame, so nothing of the new look is in it. A change
  // that leaves the effective look as it was (OLED in light mode, for Clean)
  // has nothing to fade.
  const aerie::tokens::EffectiveLook next = look();
  if (has_applied_ && next != applied_look_) {
    crossfade_.Start(client_->GetLookWidget());
  }
  SyncBrowserColorSchemeFromMode();
  Apply();
}

void SidebarLookController::OnBrowserColorSchemeChanged() {
  ThemeService* themes = ThemeServiceFactory::GetForProfile(profile_);
  if (!themes) {
    return;
  }
  const ModeChoice choice = ChoiceOf(themes->GetBrowserColorScheme());
  if (ModePref(*profile_->GetPrefs()) != choice) {
    // Written back, so the Settings page shows the mode the browser is in. This
    // runs OnAppearancePrefChanged().
    profile_->GetPrefs()->SetString(aerie::appearance::kModePref,
                                    aerie::appearance::ModeChoiceName(choice));
  }
}

void SidebarLookController::SyncBrowserColorSchemeFromMode() {
  ThemeService* themes = ThemeServiceFactory::GetForProfile(profile_);
  if (!themes) {
    return;
  }
  const ThemeService::BrowserColorScheme scheme =
      SchemeOf(ModePref(*profile_->GetPrefs()));
  if (themes->GetBrowserColorScheme() != scheme) {
    themes->SetBrowserColorScheme(scheme);
  }
}

void SidebarLookController::Apply(bool notify_client) {
  const aerie_tab_model::Workspace* workspace =
      model_ ? &model_->workspace() : nullptr;
  const aerie::tokens::EffectiveLook next = look();
  const std::string colour = aerie_look::WorkspaceColour(workspace);
  const bool changed =
      !has_applied_ || next != applied_look_ || colour != applied_colour_;
  has_applied_ = true;
  applied_look_ = next;
  applied_colour_ = colour;

  // Chromium's own surfaces. The ColorProviderKey has no field for the Theme,
  // so a new Theme or Workspace colour empties the cache of providers, and the
  // window's views build theirs again.
  const bool mixer_changed = aerie_color::SetActiveLook(next.theme, colour);
  if (mixer_changed) {
    ui::ColorProviderManager::Get().ResetColorProviderCache();
  }
  views::Widget* widget = client_->GetLookWidget();
  if ((changed || mixer_changed) && widget) {
    widget->ResetLastColorProviderKey();
    widget->ThemeChanged();
  }
  if (changed && notify_client) {
    client_->OnEffectiveLookChanged();
  }
}
