# Row padding audit (issue #205)

Read from the source of each surface, not from a running build. The Bench check `row_padding` measures
the surfaces marked "measured by `row_padding` on the train" below. Nothing has been measured on a build yet.

| Surface | Result |
| --- | --- |
| Aerie Guard page (Settings) | Was wrong: `cr-row list-item` rows lost their padding. Fixed. Measured by `row_padding` on the train. |
| Hardening page (Settings) | Rows are plain `cr-row`. No change. Measured by `row_padding` on the train. |
| Reduce motion row (Appearance) | Was wrong: `settings-row` has no padding. Now `settings-box first`. Read from the Helium source, `first` removes the top border; this row is not first on the page, so the train screenshot must check the divider above it. Measured by `row_padding` on the train. |
| Aerie Guard page, own rules entry (#107) | A `cr-row` with a textarea. No template fix needed. Required by `row_padding` (`.custom-rules-row`) on the train. |
| Fingerprinting site list | Helium's page. Aerie patches no rows there. No change. |
| About / release | The alpha notice (#199) is a `secondary` div inside Helium's existing About row, so it takes that row's padding. No new row. No change. |
| Aerie Guard panel (top-chrome WebUI) | Own layout in `app.css` with `--aerie-space-*` tokens, not Settings rows. No change. Its cards are measured by `row_padding` on the train (card rule). |
| Setup page (`aerie_onboarding`) | Own layout with a 32px shell and card padding, not Settings rows. No change. Its option cards on the look step are measured by `row_padding` on the train (card rule). |
| Widevine prompt | Native views dialog. No row markup. No change. |

The Guard page "Update" button margin uses `--cr-section-padding` (inline), not the vertical-padding token.

## Gallery

Before and after light and dark shots of the Aerie Guard page and the Reduce motion row are owned by the
build train (feature "Row padding" in the Tailscale gallery). They need a built browser. Not done here.
