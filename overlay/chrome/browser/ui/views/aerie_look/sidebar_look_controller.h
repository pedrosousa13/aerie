// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_SIDEBAR_LOOK_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_SIDEBAR_LOOK_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/ui/views/aerie_look/look_crossfade.h"
#include "components/aerie_design_tokens/aerie_effective_look.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

class Profile;

namespace aerie_tab_model {
class TabModel;
}

namespace views {
class Widget;
}

// What a window does when the effective look may have changed (issue #185).
// One per window, owned by the frame (FrameTintView). It watches the two
// appearance prefs, the browser's colour scheme and the native theme (dark,
// contrast and forced colours), and the views read the result from the same
// pure function (aerie_look::ComputeEffectiveLook), so they cannot disagree
// with it. The controller does what is not a view's job:
//
//  * It keeps the Mode in step with the browser's colour scheme, so Chromium's
//    own windows, menus and bubbles are light or dark with the sidebar: the
//    Mode pref is written to ThemeService::SetBrowserColorScheme(), and a
//    change of the colour scheme from Chromium's own Customize Chrome page is
//    written back to the Mode pref.
//  * It gives the Aerie ColorMixer the Theme and the Workspace colour. When
//    they change it resets the ColorProviderManager's cache and tells the
//    window's views (Widget::ThemeChanged), which build their providers again.
//  * It starts the crossfade for a change made in Settings, and only for that:
//    a look applied at launch, or changed by the operating system, applies at
//    once.
class SidebarLookController : public ui::NativeThemeObserver {
 public:
  class Client {
   public:
    // The window whose chrome fades, and the one whose views are told.
    virtual views::Widget* GetLookWidget() = 0;
    // The effective look may be a different one: paint again.
    virtual void OnEffectiveLookChanged() = 0;

   protected:
    ~Client() = default;
  };

  // `model` may be null.
  SidebarLookController(Profile* profile,
                        aerie_tab_model::TabModel* model,
                        Client* client);
  SidebarLookController(const SidebarLookController&) = delete;
  SidebarLookController& operator=(const SidebarLookController&) = delete;
  ~SidebarLookController() override;

  // The look the views are drawn in now.
  aerie::tokens::EffectiveLook look() const;

  // The Workspace record changed (its colour, theme or mode).
  void OnWorkspaceChanged();
  // The window got a widget and a native theme: read the look once more.
  void OnAddedToWidget();

  const LookCrossfade& crossfade_for_testing() const { return crossfade_; }
  LookCrossfade& crossfade_for_testing() { return crossfade_; }

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  // The Mode pref or the Theme pref changed (Settings, or the setup page).
  void OnAppearancePrefChanged();
  // The browser's colour scheme changed.
  void OnBrowserColorSchemeChanged();
  // Brings the mixer's look, the cache and the views up to date.
  void Apply(bool notify_client = true);
  void SyncBrowserColorSchemeFromMode();
  const ui::NativeTheme& native_theme() const;

  const raw_ptr<Profile> profile_;
  const raw_ptr<aerie_tab_model::TabModel> model_;
  const raw_ptr<Client> client_;
  LookCrossfade crossfade_;
  PrefChangeRegistrar pref_registrar_;
  base::ScopedMultiSourceObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observations_{this};
  // The (theme, colour) this window last gave the mixer.
  aerie::tokens::EffectiveLook applied_look_;
  std::string applied_colour_;
  bool has_applied_ = false;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_SIDEBAR_LOOK_CONTROLLER_H_
