# Security policy

Aerie is alpha software with one maintainer. Security comes first in this
project, so please report any weakness you find. Do not open a public issue
for it.

## How to report

Use GitHub's private vulnerability reporting:
<https://github.com/pedrosousa13/aerie/security/advisories/new>.

Tell us:

- what the problem is and what an attacker can do with it;
- the Aerie version (`chrome://settings/help`) or the commit you built;
- the steps to reproduce it, and a proof of concept if you have one.

## What happens next

- We try to acknowledge a report within 7 days. This is a one-person project,
  so it can take longer.
- We fix the problem in a private fork, credit you in the advisory (unless you
  do not want credit), and publish the advisory when the fix is released.
- We ask you to wait 90 days, or until the fix is out (whichever comes first),
  before you disclose the problem in public. If you need a different
  timeline, tell us.

There is no paid bug bounty.

## Scope

In scope: everything this repository adds to the browser and to its build.

- Aerie's patches (`patches/`) and its own source (`overlay/`, `rust/`), for
  example the Filter Engine, Aerie Guard, phishing protection, the
  fingerprinting protections, Workspaces and the WebUI pages.
- Any way the browser contacts a host that
  [docs/network-endpoints.md](docs/network-endpoints.md) does not list.
- The build and supply chain: `helium.lock`, the crate and filter-list locks,
  `./aerie` and `tools/`, and the GitHub workflows.

Out of scope. Report these upstream:

- Chromium bugs that are also in a stock Chromium build:
  <https://www.chromium.org/Home/chromium-security/reporting-security-bugs/>.
- Bugs that are also in unmodified Helium: <https://github.com/imputnet/helium>.
- Bugs in the filter lists themselves: report them to the list's publisher.

If you are not sure where a bug belongs, report it here and we will forward it.

## Supported versions

Only the newest release and `main` get security fixes. Aerie follows Helium
and Chromium releases closely (see [docs/helium-bump.md](docs/helium-bump.md)),
so a Chromium security fix arrives with the next Helium bump.

## Safe harbour

We will not take legal action against research done in good faith that
follows this policy: test only on your own accounts and devices, do not get
or keep other people's data, and do not degrade services for other people.
