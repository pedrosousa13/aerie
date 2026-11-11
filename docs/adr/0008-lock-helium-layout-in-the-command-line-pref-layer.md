# 8. Lock Helium's layout in the command-line layer of the pref service

Status: accepted (issue #215, item 6). Built in the same issue: `overlay/components/aerie_sidebar/aerie_sidebar_layout_lock.*` and `patches/sidebar/0034`.

## Context

Helium ships four tab layouts and a Zen mode. Their prefs are `helium.browser.layout` and `helium.browser.zen_mode` (`HeliumLayoutType::kVertical` is 2). Aerie's sidebar is the vertical layout underneath, so the two prefs must read "vertical" and "off" for as long as the sidebar is the layout. Neither the user nor Settings, an extension, sync or an old profile may bring another layout back (issue #177, acceptance criterion 1).

Helium's layout patches stay applied, because `tab_metrics_provider.cc`, `toolbar_view.cc` and `browser_command_controller.cc` read these prefs. Dropping `layout/core.patch` would mean re-cutting adapted copies (CONTRIBUTING.md, "Dropping or adapting a Helium patch").

The first slice of #177 (`sidebar/0021`) held the prefs by writing them back: `HeliumLayoutStateController` watched the prefs and set them again after every change. That had three faults. Another value was readable between the write and the write-back. The user store kept the foreign value, so the pref looked user-set. And the hold lived inside a Helium class, in a hook that needed two new dependencies. The review of #177 asked for a locked pref, as `patches/hardening/0006` does.

`patches/hardening/0006` does not fit. It sets the write permission of an extension API entry in `pref_mapping.cc` to `kInvalid`, and the Helium layout prefs have no entry there. The layout prefs are not reachable by an extension at all. The user can still write them through Settings (`settings_private`), an old profile file and sync.

## Decision

1. **A fixed value in the command-line layer.** `ChromeCommandLinePrefStore`, the command-line layer of every `PrefService` Chrome builds, calls `aerie_sidebar::LockHeliumLayout(this)` after it has read its switches. That puts `helium.browser.layout` = 2 and `helium.browser.zen_mode` = false in the store. The pref service reads a layer in this order: policy, supervised user, extension, command line, user, recommended, default. A value in the command-line layer wins over the user's own file, so:
   - `PrefService::GetInteger` returns 2 and `GetBoolean` returns false, whatever the user file holds. An old profile with another layout opens as the sidebar.
   - `Preference::IsUserModifiable()` is false for both prefs. A `SetInteger` changes only the user file, and no observer is called, because the effective value did not change. So nothing has to be written back.
   - The settings_private allowlist no longer lists the layout, Zen mode and the two Zen pin prefs (`sidebar/0021`), so the Settings page cannot read or write them. There is no Settings row to show as locked: the layout picker is gone (`sidebar/0022`).
   - An extension cannot reach them: they have no `chrome.privacy` or `chrome.settingsPrivate` entry, and the extension layer is below nothing Aerie fills.
2. **The hook is one call.** The values and the logic are in the overlay. The upstream patch adds an include, one line in the constructor and one GN dependency.
3. **The old write-back is removed.** `EnforceHeliumLayout`, its call in `HeliumLayoutStateController` and the two GN dependencies of `chrome/browser/ui/helium` are gone.
4. **A fresh profile still registers the vertical layout as its default** (`sidebar/0021`), so a code path that reads the pref from a store without the lock (a unit test, a tool) also sees the sidebar.

## Consequences

**Good**

- The pref is not "set and then fixed": it is never anything else. Nothing flickers, and no observer sees a wrong value.
- The user's own file cannot weaken the lock, and the lock does not write to the user's file.
- The hook is the smallest that does the job, and a Helium bump that renames the prefs fails `tests/test_sidebar.py`, which compares the names with Helium's patches.

**Bad**

- The command-line layer is meant for command-line switches. Using it for a fixed value is a small misuse. It was chosen because it needs no policy definition, no policy provider and no change to how the factory builds its stores, and because an enterprise policy (the managed layer, above it) can still win for a deployment that sets one. Nothing in Aerie reads the layer for another reason.
- `chrome://policy` and the Settings "managed by your organisation" notice do not show these prefs: they are locked, not managed. That is intended: the user is not told a rule exists, because there is no row to explain it.
- The lock is unconditional. Issue #186 adds a horizontal layout (`aerie.sidebar.layout` = `horizontal`). It must make the lock follow that pref: `LockHeliumLayout` becomes a function of the store and the layout, and the store's value is replaced (with `SetValue`, which notifies the observers) when the layout changes. The Local State service gets the same two values and ignores them, because it does not register the prefs.

## Alternatives considered

### Keep the write-back hold

Works, but is the fault list above. Rejected by the review of #177.

### Managed layer through a policy

A `ConfigurationPolicyPrefStore` fed by a policy handler needs a policy definition in `policy_templates`, a `ConfigurationPolicyHandler` and a provider that is always present. It is several files in upstream directories, and it would show the prefs as "managed by your organisation" in `chrome://policy`. Rejected as much larger for the same effect.

### A custom managed pref store

`PrefServiceFactory` takes one managed store, which `SetManagedPolicies` fills from the policy service. A second store would have to merge with it, and `PrepareFactory` would change in several places (profile, Local State, the supervised variant). Rejected.

### Drop `layout/core.patch`

Removes the prefs, and with them the readers in `tab_metrics_provider.cc`, `toolbar_view.cc` and `browser_command_controller.cc`. Needs adapted copies of several Helium patches and a re-cut at every bump. Rejected (#177).
