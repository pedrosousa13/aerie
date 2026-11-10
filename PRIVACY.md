# Privacy policy

Aerie is a web browser that runs on your computer. It has no account, no
telemetry, no analytics and no crash upload. The Aerie project runs no server,
so it receives nothing from the browser.

This policy covers what the browser sends on its own. The pages you visit, the
searches you run and the extensions you install have their own privacy
policies.

## What the browser sends, and to whom

| When | Host | What is sent |
| --- | --- | --- |
| Filter list updates: once a day (the first check is a day after install), or when you click update on `chrome://filters` | `easylist.to` | A plain https request for `easylist.txt` and `easyprivacy.txt`. No cookies and no referrer. You can turn updates off on `chrome://filters`. |
| A custom filter list you add | The https address you entered | The same kind of request, on the same schedule. |
| Only after you turn on Widevine (protected video) | `update.googleapis.com`, then the Google download host named in its reply | An update check that names only the Widevine component, then the download of the component. Off by default. |
| Only after you set an extension proxy in Settings | The https address you entered | Extension downloads and update checks. Not set by default. |
| Only after you turn on secure DNS | The DNS provider you chose | Your DNS queries. Off by default. |
| Only after you turn on search suggestions | Your search engine (DuckDuckGo by default) | What you type in the address bar, as you type. Off by default. |

The phishing and malware lists ship inside the browser. Aerie checks pages
against them on your computer and sends nothing to anyone.

The browser does not contact Google or Helium (the browser Aerie is built on)
except in the Widevine row above. The full list of endpoints, and how each one
was removed, is in [docs/network-endpoints.md](docs/network-endpoints.md). An
automated test (the Bench check `zero_traffic`) records the browser's traffic
to prove it.

## What stays on your computer

Your history, bookmarks, passwords, cookies, Workspaces, settings and site
exceptions stay in your browser profile on your computer. Aerie has no sync,
so none of this is copied anywhere.

## Changes

A change to what the browser sends changes this page in the same commit. The
history of this file is the history of the policy.

## Contact

Open an issue at <https://github.com/pedrosousa13/aerie/issues>. For a
security problem, see [SECURITY.md](SECURITY.md).
