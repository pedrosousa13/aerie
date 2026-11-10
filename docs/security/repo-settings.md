# Repository protection

How the GitHub repo is protected, and why. These are GitHub settings, not
files, so this page is the record. Change both together.

## `main`

Branch protection on `main`, applied to admins too:

- Every change lands through a pull request. No review approval is required,
  because Aerie has one maintainer and GitHub does not let an author approve
  their own pull request. The build train's review and the local CI gate
  ([ci.md](../ci.md)) stand in for a second reviewer.
- These checks must pass: the four CodeQL jobs (`analyze (actions)`,
  `analyze (javascript-typescript)`, `analyze (python)`, `analyze (rust)`) and
  the secret scan (`gitleaks`).
  The Rust audit is not required, because it runs only when
  `rust/crates.lock.toml` changes, and a required check that never runs blocks
  the pull request forever.
- Linear history. Pull requests are squash-merged, and GitHub signs the
  squash commit, so every commit on `main` is verified. "Require signed
  commits" is off on purpose: GitHub checks it against the pull request's own
  commits, and agents commit without a signing key, so it blocks every merge.
- No force-push and no deletion.

## Merging

- Squash merge only. Merge commits and rebase merges are off.
- Auto-merge is on: `gh pr merge <n> --squash --auto` merges as soon as the
  required checks pass. Agents in other sessions use this.
- Head branches are deleted after the merge.

## Tags

The ruleset "Protect tags" stops any tag from being deleted, moved or
force-updated. New tags can still be created, so releases (#130) work.

## GitHub Actions

- The default `GITHUB_TOKEN` is read-only, and workflows cannot approve pull
  requests. Each workflow asks for the extra permissions it needs.
- Only GitHub-owned actions and `ossf/scorecard-action` may run, and every
  action must be pinned to a full commit SHA. Dependabot keeps the pins
  current (`.github/dependabot.yml`). Tools that are not actions (gitleaks,
  cargo-audit) are pinned by version, and gitleaks by sha256 as well.
- Workflows on pull requests from outside contributors wait for the
  maintainer's approval, on every pull request, not only the first.

## Security features

- Private vulnerability reporting ([SECURITY.md](../../SECURITY.md)).
- Dependabot alerts and security updates.
- Secret scanning with push protection (GitHub's provider patterns).
  Non-provider patterns and validity checks need GitHub Advanced Security and
  are off. Gitleaks covers the generic patterns instead: it scans the whole
  history on every pull request and weekly (`secret-scan.yml`, rules in
  `.gitleaks.toml`). It runs offline. To run it locally:
  `gitleaks git . --config .gitleaks.toml --redact`.
- Code scanning: CodeQL and OpenSSF Scorecard
  (`.github/workflows/codeql.yml`, `scorecard.yml`), and the daily RustSec
  audit (`rust-audit.yml`).
- The wiki is off. Documentation lives in the repo.
