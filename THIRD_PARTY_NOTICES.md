# Third-party notices

Aerie is built from other people's work. Each part keeps its own licence. The
browser shows the licence of every library it contains on `chrome://credits`.

| Part | Where | Licence | Licence text |
| --- | --- | --- | --- |
| Chromium | Downloaded by `./aerie build`, pinned in `helium.lock` | BSD-3-Clause, plus the licences of its third-party code | The `LICENSE` file and `third_party/*/LICENSE` files in the source tree; `chrome://credits` |
| ungoogled-chromium | Through Helium (`helium-chromium/`) | BSD-3-Clause | [helium-chromium/LICENSE.ungoogled_chromium](helium-chromium/LICENSE.ungoogled_chromium) |
| Helium | `helium-chromium/`, `helium-linux/` (submodules) | GPL-3.0 | [helium-chromium/LICENSE](helium-chromium/LICENSE) |
| adblock-rust (Brave) and the crates it adds | `rust/crates.lock.toml`, vendored into Chromium's crates by the build | adblock-rust: MPL-2.0; each crate under its own licence | `chrome://credits` (the build adds the crates' credits metadata) |
| EasyList, EasyPrivacy | `overlay/third_party/filter_lists/` | GPL-3.0-or-later (dual licensed with CC BY-SA 3.0; Aerie uses the GPL) | [overlay/third_party/filter_lists/LICENSE](overlay/third_party/filter_lists/LICENSE) |
| malware-filter phishing-filter (curbengh) | `overlay/third_party/filter_lists/phishing_filter_hosts.txt` | CC BY-SA 4.0 | [overlay/third_party/filter_lists/LICENSE.phishing_filter](overlay/third_party/filter_lists/LICENSE.phishing_filter) |

Aerie's own code (its patches, `overlay/`, `rust/`, `tools/`, `tests/`) is
licensed under the GNU General Public License, version 3 or (at your option)
any later version. The browser as a whole is distributed under GPL-3.0, the
licence of Helium. See [LICENSE](LICENSE).

Brand names belong to their owners. Aerie is not affiliated with Google,
Brave, Helium or the list publishers.
