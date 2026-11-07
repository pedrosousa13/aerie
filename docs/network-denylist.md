# Network denylist

The Bench check `zero_traffic` fails if the browser requests any host on this
list. The list lives in `tools/aerie_bench/denylist/google-helium.txt`; the
matcher is `tools/aerie_bench/denylist.py`. A test checks that every pattern in
the file is named on this page, so the two cannot drift apart.

## How matching works

- A domain pattern matches the domain and every subdomain, on label
  boundaries. `gstatic.com` matches `fonts.gstatic.com`. It does not match
  `notgstatic.com` or `gstatic.com.evil.example`.
- `*.name` means the same as `name`.
- `google.*` matches `google` followed by a TLD: `google.de`, `www.google.co.uk`,
  `google.com.br`. It does not match `google.example.com`.
- A bare label (`google`) is a TLD rule: it matches `dns.google`, not `google.com`.
- `url host/prefix` matches one host and a path that starts with the prefix.
  It is used where the host is shared with other people (GitHub).
- Path rules (`url ...`) compare the path in lower case.
- Names are lower-cased, a trailing dot is dropped, and IDN names are compared
  in punycode. IP addresses never match.
- `.qjz9zk` is not on the list. It is the dummy TLD that the domain
  substitution step rewrites Google hosts into. The network stack refuses
  these requests before any DNS query, but the attempt still shows in the
  NetLog. The check reports them as "blocked attempts": they do not fail the
  check unless you pass `--zero-traffic-strict`.

## Google

| Pattern | What it covers |
| --- | --- |
| `google.*` | Search, accounts, `clients2.google.com`, `chromewebstore.google.com`, every country domain |
| `google` | The `.google` TLD, for example `dns.google` (Google Public DNS over HTTPS) |
| `gstatic.com` | Static content, favicons (`t0.gstatic.com`), WebAuthn related origins |
| `googleapis.com`, `googleapis.cn` | APIs, `update.googleapis.com` (component updater), fonts |
| `gvt1.com`, `gvt2.com`, `gvt3.com` | Chrome downloads, spellcheck dictionaries (`redirector.gvt1.com`) |
| `googleusercontent.com`, `googlevideo.com`, `ggpht.com`, `googlezip.net` | User content, video, photos, data compression proxy |
| `doubleclick.net`, `google-analytics.com`, `googletagmanager.com`, `googletagservices.com`, `googlesyndication.com`, `googleadservices.com`, `googleoptimize.com`, `recaptcha.net` | Ads, analytics and reCAPTCHA |
| `youtube.com`, `youtube-nocookie.com`, `youtu.be`, `ytimg.com` | Video |
| `gmail.com`, `googlemail.com`, `goo.gl`, `1e100.net`, `withgoogle.com`, `googlesource.com`, `ampproject.org` | Mail, short links, infrastructure names, product sites, source hosting, AMP |
| `chromium.org`, `chrome.com`, `android.com` | Project hosts: variations, crash, docs links |
| `appspot.com` | Google-owned app hosting. Third parties use it too, but the Bench loads only local pages, so a hit comes from the browser itself |

## Helium and imput

| Pattern | What it covers |
| --- | --- |
| `helium.computer` | `updates.helium.computer` (browser updates), `crash.helium.computer` (crash upload), the project site |
| `imput.net` | `services.helium.imput.net`: the Helium services proxy (component updates, extensions, bangs, uBlock Origin assets, spellcheck) |
| `imput.dev` | Other imput hosts. Not seen in the tree; listed so a new one cannot slip in |
| `url raw.githubusercontent.com/imputnet/` | Helium filter lists and uBlock Origin fork assets |
| `url github.com/imputnet/` | Helium repositories |
| `url imputnet.github.io/` | Helium pages on GitHub Pages |

GitHub as a whole is not listed: only the imput paths are.

## Known limits

- **Direct-IP requests are not matched.** The list is about names. A request
  to a Google IP address, with no name involved, is invisible to it.
- The check reads host names from URL requests and from resolver, connect and
  socket events in the NetLog. The Bench's resolver rules rewrite names to
  `~notfound` before some of those events are logged, so DNS-level matching
  catches less than it would on an open network. URL requests still carry the
  real name.

## What the list is not

It is not a list of every tracker. It lists the owners the issue names:
Google and Helium/imput. Other hosts are reported in the check's results
(`other_hosts`) for review but do not fail it. A search engine host that
Aerie contacts on purpose (DuckDuckGo, Brave, Kagi, Startpage) is not Google
and not Helium.

## Changing the list

Add the pattern to the file and to the table above, in the same change.
Run `python3 -m unittest discover -s tests -t .`.
