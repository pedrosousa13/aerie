# Search

Aerie offers four built-in search engines: **DuckDuckGo** (the default),
**Brave Search**, **Kagi** and **Startpage**. There is no Google, Bing, Ecosia
or Qwant entry. The list is the default list in
`third_party/search_engines_data/resources/definitions/regional_settings.json`,
trimmed by `patches/search/0001-trim-the-search-engine-list.patch`. A source
gate (`./aerie patches gates`) checks the list and the order.

**Search suggestions are off by default.** Nothing is sent to the search
engine until you press Enter. You can turn suggestions on in Settings >
Search engine; Aerie does not turn them on for you. A source gate checks the
default.

## A custom engine, for example SearXNG

Use Chromium's own custom search engine feature: Settings > Search engine >
Manage search engines and site search > Add.

| Field | Value for SearXNG |
| --- | --- |
| Name | SearXNG |
| Shortcut | `sx` |
| URL | `https://YOUR-INSTANCE/search?q=%s` |

`%s` is where the query goes. Use an `https` instance that you run or trust.
Make it the default from the same page if you want.

## Secure DNS

Secure DNS (DNS over HTTPS) is off by default. Every provider stays selectable
in Settings > Privacy and security > Security > Use secure DNS.
