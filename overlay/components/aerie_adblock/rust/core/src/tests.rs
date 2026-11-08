// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

use super::*;

const LIST: &str = "\
! Title: test list
||ads.example^$script
||tracker.example^
||img-only.example^$image
@@||tracker.example/allowed/
||everything.example^$important
@@||everything.example^
/banner-[0-9]+\\.gif/
||third.example^$third-party
";

fn engine() -> FilterEngine {
    FilterEngine::from_lists([LIST.as_bytes()]).unwrap()
}

fn req<'a>(
    url: &'a str,
    host: &'a str,
    source: &'a str,
    kind: RequestKind,
    third: bool,
) -> RequestInfo<'a> {
    RequestInfo {
        url,
        hostname: host,
        source_hostname: source,
        kind,
        third_party: third,
    }
}

#[test]
fn blocks_listed_url_with_matching_type() {
    let e = engine();
    assert!(e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "news.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn type_option_is_respected() {
    let e = engine();
    // `||ads.example^$script` does not cover images.
    assert!(!e.should_block(&req(
        "https://ads.example/a.png",
        "ads.example",
        "news.test",
        RequestKind::Image,
        true
    )));
    assert!(e.should_block(&req(
        "https://img-only.example/a.png",
        "img-only.example",
        "news.test",
        RequestKind::Image,
        true
    )));
    assert!(!e.should_block(&req(
        "https://img-only.example/a.js",
        "img-only.example",
        "news.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn first_party_content_is_allowed() {
    let e = engine();
    assert!(!e.should_block(&req(
        "https://news.test/app.js",
        "news.test",
        "news.test",
        RequestKind::Script,
        false
    )));
}

#[test]
fn third_party_option_uses_the_flag_the_caller_gives() {
    let e = engine();
    let url = "https://third.example/x";
    assert!(e.should_block(&req(
        url,
        "third.example",
        "news.test",
        RequestKind::Script,
        true
    )));
    assert!(!e.should_block(&req(
        url,
        "third.example",
        "third.example",
        RequestKind::Script,
        false
    )));
}

#[test]
fn exception_rules_win() {
    let e = engine();
    assert!(e.should_block(&req(
        "https://tracker.example/t.gif",
        "tracker.example",
        "news.test",
        RequestKind::Image,
        true
    )));
    assert!(!e.should_block(&req(
        "https://tracker.example/allowed/t.gif",
        "tracker.example",
        "news.test",
        RequestKind::Image,
        true
    )));
}

#[test]
fn important_beats_exception() {
    let e = engine();
    assert!(e.should_block(&req(
        "https://everything.example/x",
        "everything.example",
        "news.test",
        RequestKind::Other,
        true
    )));
}

#[test]
fn regex_rules_work() {
    let e = engine();
    assert!(e.should_block(&req(
        "https://cdn.test/banner-42.gif",
        "cdn.test",
        "news.test",
        RequestKind::Image,
        true
    )));
    assert!(!e.should_block(&req(
        "https://cdn.test/banner-x.gif",
        "cdn.test",
        "news.test",
        RequestKind::Image,
        true
    )));
}

#[test]
fn empty_engine_blocks_nothing() {
    let e = FilterEngine::empty();
    assert!(!e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "news.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn empty_url_or_host_is_not_blocked_and_does_not_panic() {
    let e = engine();
    assert!(!e.should_block(&req("", "", "", RequestKind::Other, false)));
    assert!(!e.should_block(&req(
        "https://ads.example/a.js",
        "",
        "",
        RequestKind::Script,
        true
    )));
}

#[test]
fn odd_urls_do_not_panic() {
    let e = engine();
    for url in [
        "data:text/plain,hi",
        "blob:https://a.test/uuid",
        "javascript:void(0)",
        ":",
        "\u{1F600}:x",
        "https://\u{FFFD}/",
    ] {
        let _ = e.should_block(&req(url, "x.test", "y.test", RequestKind::Other, true));
    }
}

#[test]
fn serialize_round_trip_keeps_behaviour() {
    let bytes = engine().serialize();
    assert!(!bytes.is_empty());
    let mut loaded = FilterEngine::empty();
    loaded.load_serialized(&bytes).unwrap();
    assert!(loaded.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "news.test",
        RequestKind::Script,
        true
    )));
    assert!(!loaded.should_block(&req(
        "https://news.test/app.js",
        "news.test",
        "news.test",
        RequestKind::Script,
        false
    )));
}

#[test]
fn bad_serialized_engine_is_an_error_and_keeps_the_old_engine() {
    let mut e = engine();
    for bad in [&b""[..], b"garbage", &[0u8; 64][..], &[0xffu8; 4096][..]] {
        assert_eq!(e.load_serialized(bad), Err(LoadError::BadEngine));
    }
    // A truncated real engine fails the checksum.
    let good = engine().serialize();
    assert_eq!(
        e.load_serialized(&good[..good.len() / 2]),
        Err(LoadError::BadEngine)
    );
    // A flipped byte fails too.
    let mut flipped = good.clone();
    let mid = flipped.len() / 2;
    flipped[mid] ^= 0x55;
    assert_eq!(e.load_serialized(&flipped), Err(LoadError::BadEngine));
    // The engine that was there before still works.
    assert!(e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "news.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn oversize_inputs_are_refused() {
    let huge = vec![b'a'; MAX_LIST_BYTES + 1];
    assert_eq!(
        FilterEngine::from_lists([&huge[..]]).err(),
        Some(LoadError::TooLarge)
    );
    let mut e = FilterEngine::empty();
    let huge = vec![0u8; MAX_SERIALIZED_BYTES + 1];
    assert_eq!(e.load_serialized(&huge), Err(LoadError::TooLarge));
}

// Malformed lists: the engine must build, never panic, and still honour the
// good lines around the bad ones.

#[test]
fn malformed_lines_are_skipped_and_good_rules_survive() {
    let list = "\
||ads.example^$script
$$$$
||
@@
||x^$unknownoption=1,script
/(((((/
/[/
||bad.example^$domain=
||tracker.example^
";
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    assert!(e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "n.test",
        RequestKind::Script,
        true
    )));
    assert!(e.should_block(&req(
        "https://tracker.example/x",
        "tracker.example",
        "n.test",
        RequestKind::Other,
        true
    )));
}

#[test]
fn binary_garbage_list_does_not_panic() {
    let mut garbage = Vec::new();
    let mut x: u32 = 0x1234_5678;
    for _ in 0..200_000 {
        x = x.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        garbage.push((x >> 24) as u8);
    }
    let e = FilterEngine::from_lists([&garbage[..]]).unwrap();
    let _ = e.should_block(&req(
        "https://a.test/x",
        "a.test",
        "b.test",
        RequestKind::Other,
        true,
    ));
}

#[test]
fn invalid_utf8_nul_and_crlf_are_handled() {
    let mut list = b"||ads.example^$script\r\n".to_vec();
    list.extend_from_slice(&[0xff, 0xfe, 0x00, b'\n']);
    list.extend_from_slice(b"||tracker.example^\r\n");
    let e = FilterEngine::from_lists([&list[..]]).unwrap();
    assert!(e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "n.test",
        RequestKind::Script,
        true
    )));
    assert!(e.should_block(&req(
        "https://tracker.example/x",
        "tracker.example",
        "n.test",
        RequestKind::Other,
        true
    )));
}

#[test]
fn overlong_lines_are_dropped_not_parsed() {
    let mut list = String::from("||ads.example^$script\n");
    list.push_str("||");
    list.push_str(&"a".repeat(MAX_LINE_BYTES + 1));
    list.push_str("^\n/");
    list.push_str(&"(a|b)*".repeat(MAX_LINE_BYTES));
    list.push_str("/\n||tracker.example^\n");
    assert!(sanitize_list(list.as_bytes()).len() < 100);
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    assert!(e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "n.test",
        RequestKind::Script,
        true
    )));
    assert!(e.should_block(&req(
        "https://tracker.example/x",
        "tracker.example",
        "n.test",
        RequestKind::Other,
        true
    )));
}

#[test]
fn regex_bomb_rule_does_not_hang_matching() {
    // Nested quantifiers: catastrophic in a backtracking engine. The regex
    // crate is linear time, so this must return promptly.
    let list = "/^(a+)+$/\n/(x+x+)+y/\n";
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    let url = format!("https://a.test/{}", "a".repeat(5000));
    let _ = e.should_block(&req(&url, "a.test", "b.test", RequestKind::Other, true));
}

#[test]
fn a_huge_number_of_rules_builds() {
    let mut list = String::new();
    for i in 0..50_000 {
        list.push_str(&format!("||host{i}.example^$script\n"));
    }
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    assert!(e.should_block(&req(
        "https://host49999.example/a.js",
        "host49999.example",
        "n.test",
        RequestKind::Script,
        true
    )));
    assert!(!e.should_block(&req(
        "https://host50000.example/a.js",
        "host50000.example",
        "n.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn multiple_lists_combine() {
    let a = b"||one.example^\n";
    let b = b"||two.example^\n";
    let e = FilterEngine::from_lists([&a[..], &b[..]]).unwrap();
    assert!(e.should_block(&req(
        "https://one.example/",
        "one.example",
        "n.test",
        RequestKind::Other,
        true
    )));
    assert!(e.should_block(&req(
        "https://two.example/",
        "two.example",
        "n.test",
        RequestKind::Other,
        true
    )));
}

#[test]
fn request_kind_names_match_the_filter_syntax() {
    use RequestKind::*;
    let names: Vec<_> = [
        Other,
        Document,
        Subdocument,
        Script,
        Stylesheet,
        Image,
        Font,
        Media,
        XmlHttpRequest,
        Ping,
        Websocket,
        Object,
    ]
    .iter()
    .map(|k| k.as_filter_type())
    .collect();
    assert_eq!(
        names,
        [
            "other",
            "document",
            "subdocument",
            "script",
            "stylesheet",
            "image",
            "font",
            "media",
            "xmlhttprequest",
            "ping",
            "websocket",
            "object"
        ]
    );
}

#[test]
fn a_match_all_exception_line_cannot_switch_blocking_off() {
    for bad in ["@@", "@@$image", "@@*", "@@||", "@@|$script", "@@^"] {
        let list = format!("||ads.example^$script\n{bad}\n");
        let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
        assert!(
            e.should_block(&req(
                "https://ads.example/a.js",
                "ads.example",
                "n.test",
                RequestKind::Script,
                true
            )),
            "{bad:?} switched blocking off"
        );
    }
    // A real exception is untouched.
    let list = "||ads.example^\n@@||ads.example/ok$script\n";
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    assert!(!e.should_block(&req(
        "https://ads.example/ok",
        "ads.example",
        "n.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn object_type_rules_apply_to_object_requests_only() {
    let e = FilterEngine::from_lists([&b"||plugin.example^$object\n"[..]]).unwrap();
    assert!(e.should_block(&req(
        "https://plugin.example/x.swf",
        "plugin.example",
        "n.test",
        RequestKind::Object,
        true
    )));
    assert!(!e.should_block(&req(
        "https://plugin.example/x.swf",
        "plugin.example",
        "n.test",
        RequestKind::Other,
        true
    )));
    assert!(!e.should_block(&req(
        "https://plugin.example/x.js",
        "plugin.example",
        "n.test",
        RequestKind::Script,
        true
    )));
}

// ---- Cosmetic filtering (issue #9) ----

const COSMETIC_LIST: &str = "\
! Title: cosmetic test list
##.ad-banner
###ad-slot
##.promo > .sponsored
##div[class*=\"Ads\"]
news.test##.news-only
other.test##.other-only
##.keep-me
news.test#@#.keep-me
##.always-except
#@#.always-except
||ads.example^
@@||nogeneric.test^$generichide
";

fn cosmetic_engine() -> FilterEngine {
    FilterEngine::from_lists([COSMETIC_LIST.as_bytes()]).unwrap()
}

fn lines(css: &str) -> Vec<&str> {
    css.lines().collect()
}

#[test]
fn page_stylesheet_has_domain_specific_and_complex_generic_hides() {
    let css = cosmetic_engine().cosmetic_stylesheet("https://news.test/article");
    let l = lines(&css);
    assert!(l.contains(&".news-only{display:none!important}"), "{css}");
    assert!(
        l.contains(&"div[class*=\"Ads\"]{display:none!important}"),
        "{css}"
    );
}

#[test]
fn domain_specific_rules_do_not_leak_to_other_sites() {
    let css = cosmetic_engine().cosmetic_stylesheet("https://news.test/");
    assert!(!css.contains(".other-only"), "{css}");
    let css = cosmetic_engine().cosmetic_stylesheet("https://elsewhere.test/");
    assert!(!css.contains(".news-only"), "{css}");
}

#[test]
fn exception_rules_remove_a_hide() {
    let e = cosmetic_engine();
    let generic = e.generic_stylesheet("https://news.test/", "keep-me ad-banner", "");
    assert!(
        generic.contains(".ad-banner{display:none!important}"),
        "{generic}"
    );
    assert!(
        !generic.contains(".keep-me"),
        "exception on news.test: {generic}"
    );
    let other = e.generic_stylesheet("https://elsewhere.test/", "keep-me", "");
    assert!(
        other.contains(".keep-me{display:none!important}"),
        "{other}"
    );
}

#[test]
fn generic_hides_apply_to_classes_and_ids_on_the_page() {
    let e = cosmetic_engine();
    let css = e.generic_stylesheet("https://news.test/", "ad-banner plain", "ad-slot content");
    assert!(css.contains(".ad-banner{display:none!important}"), "{css}");
    assert!(css.contains("#ad-slot{display:none!important}"), "{css}");
    // A rule that starts with a class is keyed by that class.
    let complex = e.generic_stylesheet("https://news.test/", "promo", "");
    assert!(
        complex.contains(".promo > .sponsored{display:none!important}"),
        "{complex}"
    );
    assert!(!css.contains("plain"), "{css}");
    assert!(!css.contains("content"), "{css}");
}

#[test]
fn generic_hides_need_a_matching_class_or_id() {
    let css =
        cosmetic_engine().generic_stylesheet("https://news.test/", "something-else", "other-id");
    assert!(css.is_empty(), "{css}");
}

#[test]
fn generichide_exception_switches_generic_hides_off_for_the_site() {
    let e = cosmetic_engine();
    let page = e.cosmetic_stylesheet("https://nogeneric.test/");
    assert!(!page.contains("div[class*"), "{page}");
    let generic = e.generic_stylesheet("https://nogeneric.test/", "ad-banner", "ad-slot");
    assert!(generic.is_empty(), "{generic}");
}

#[test]
fn first_party_content_selectors_are_not_hidden() {
    let e = cosmetic_engine();
    let css = format!(
        "{}{}",
        e.cosmetic_stylesheet("https://news.test/"),
        e.generic_stylesheet("https://news.test/", "article headline", "content main")
    );
    assert!(
        !css.contains("article") && !css.contains("headline") && !css.contains("#main"),
        "{css}"
    );
}

#[test]
fn stylesheet_survives_serialization() {
    let mut fresh = FilterEngine::empty();
    fresh
        .load_serialized(&cosmetic_engine().serialize())
        .unwrap();
    assert!(
        fresh
            .cosmetic_stylesheet("https://news.test/")
            .contains(".news-only")
    );
    assert!(
        fresh
            .generic_stylesheet("https://news.test/", "ad-banner", "")
            .contains(".ad-banner")
    );
}

#[test]
fn network_blocking_still_works_with_cosmetic_rules_loaded() {
    let e = cosmetic_engine();
    assert!(e.should_block(&req(
        "https://ads.example/a.js",
        "ads.example",
        "news.test",
        RequestKind::Script,
        true
    )));
}

#[test]
fn each_selector_is_its_own_rule() {
    // One bad selector in a comma list would void the whole rule in CSS.
    let css = cosmetic_engine().cosmetic_stylesheet("https://news.test/");
    for line in css.lines() {
        assert!(line.ends_with("{display:none!important}"), "{line}");
        let sel = line.trim_end_matches("{display:none!important}");
        assert!(!sel.contains("{") && !sel.contains("}"), "{line}");
    }
}

#[test]
fn unsafe_selectors_never_reach_the_stylesheet() {
    let hostile = "\
##.ok-one
##.a{background:url(https://evil.example/x)}
##.b;color:red
##.c } body { display:none
##.d/*
##.e*/
##.f\\
##.g<script>
##a[href=\"x
##.h:style(background:red)
##.i:remove()
##@import url(x)
##.url(x)
##^script:has-text(x)
##+js(abort-on-property-read, x)
news.test##+js(set-constant, a, b)
news.test##.ok-two
";
    let e = FilterEngine::from_lists([hostile.as_bytes()]).unwrap();
    let css = e.cosmetic_stylesheet("https://news.test/");
    let generic = e.generic_stylesheet("https://news.test/", "ok-one a b c d e f g h i url", "");
    let all = format!("{css}\n{generic}");
    assert!(all.contains(".ok-two{display:none!important}"), "{all}");
    assert!(all.contains(".ok-one{display:none!important}"), "{all}");
    for bad in [
        "evil",
        "background",
        "color:red",
        "body",
        "script",
        "@import",
        ":style",
        ":remove",
        "url(",
        "\\",
        "/*",
        "*/",
    ] {
        assert!(!all.contains(bad), "{bad:?} leaked: {all}");
    }
    assert_eq!(all.lines().filter(|l| !l.is_empty()).count(), 2, "{all}");
}

#[test]
fn selector_safety_rules() {
    for ok in [
        ".ad",
        "#ad",
        "div.ad > p + span ~ b",
        "a[href^=\"https://x.test/\"]",
        "ins[data-ad='1']",
        ".a:not(.b)",
        ".a, .b",
    ] {
        // A comma list is split before it gets here; the checker still accepts plain CSS.
        assert!(is_safe_selector(ok), "{ok}");
    }
    for bad in [
        "",
        " ",
        "a{",
        "a}",
        "a;b",
        "a<b",
        "a\\b",
        "a/*b",
        "a*/b",
        "a\"b",
        "a[b='c",
        "a(b",
        "a)b(",
        "a[b",
        "@media",
        "^script",
        "+js(x)",
        "a\nb",
        "a\0b",
        ".x:style(a:b)",
        ".x:remove()",
    ] {
        assert!(!is_safe_selector(bad), "{bad:?}");
    }
    assert!(!is_safe_selector(&format!(
        ".{}",
        "a".repeat(MAX_SELECTOR_BYTES)
    )));
}

#[test]
fn hide_stylesheet_skips_unsafe_and_caps_size() {
    assert_eq!(
        hide_stylesheet([".b", "a{", ".a"]),
        ".a{display:none!important}\n.b{display:none!important}\n"
    );
    let many: Vec<String> = (0..MAX_COSMETIC_SELECTORS + 50)
        .map(|i| format!(".c{i}"))
        .collect();
    assert!(hide_stylesheet(many).lines().count() <= MAX_COSMETIC_SELECTORS);
    let big: Vec<String> = (0..2000)
        .map(|i| format!(".{}{i}", "x".repeat(1500)))
        .collect();
    assert!(hide_stylesheet(big).len() <= MAX_COSMETIC_CSS_BYTES);
}

#[test]
fn page_tokens_are_limited_and_hostile_tokens_ignored() {
    let e = cosmetic_engine();
    let classes = format!("{} ad-banner", "z ".repeat(MAX_PAGE_TOKENS * 2));
    // Past the cap the tail is ignored, never a panic.
    let _ = e.generic_stylesheet("https://news.test/", &classes, "");
    let long = "q".repeat(MAX_TOKEN_BYTES + 1);
    let css = e.generic_stylesheet("https://news.test/", &long, "ad-slot\u{0}x");
    assert!(css.is_empty(), "{css}");
}

#[test]
fn odd_urls_do_not_panic_for_cosmetics() {
    let e = cosmetic_engine();
    for url in [
        "",
        "not a url",
        "https://",
        "data:text/html,x",
        "https://[::1]/",
        "https://%00/",
        &"a".repeat(100_000),
    ] {
        let _ = e.cosmetic_stylesheet(url);
        let _ = e.generic_stylesheet(url, "ad-banner", "ad-slot");
    }
}

#[test]
fn scriptlet_and_procedural_rules_are_not_kept_in_the_engine() {
    let list = "news.test##+js(abort-on-property-read, x)\nnews.test#?#div:has-text(Ad)\nnews.test#$#body { x: y }\n";
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    assert!(e.cosmetic_stylesheet("https://news.test/").is_empty());
    assert!(
        sanitize_list(list.as_bytes()).trim().is_empty(),
        "{}",
        sanitize_list(list.as_bytes())
    );
}

#[test]
fn sanitize_keeps_plain_hide_rules_and_exceptions() {
    let kept = sanitize_list(b"##.a\nx.test##.b\nx.test#@#.b\n! comment\n||net.example^\n");
    for want in ["##.a", "x.test##.b", "x.test#@#.b", "||net.example^"] {
        assert!(kept.lines().any(|l| l == want), "{want}: {kept}");
    }
}

#[test]
fn domain_resolver_finds_the_registrable_domain() {
    use adblock::url_parser::ResolvesDomain;
    let dom = |h: &str| {
        let (a, b) = AerieDomainResolver.get_host_domain(h);
        h[a..b].to_string()
    };
    assert_eq!(dom("news.test"), "news.test");
    assert_eq!(dom("a.b.news.test"), "news.test");
    assert_eq!(dom("www.example.co.uk"), "example.co.uk");
    assert_eq!(dom("example.co.uk"), "example.co.uk");
    assert_eq!(dom("localhost"), "localhost");
    assert_eq!(dom("127.0.0.1"), "127.0.0.1");
    assert_eq!(dom("[::1]"), "[::1]");
    assert_eq!(dom(""), "");
}

#[test]
fn entity_rules_match_every_tld_of_a_site() {
    let e = FilterEngine::from_lists(["shop.*##.entity-ad\n".as_bytes()]).unwrap();
    assert!(
        e.cosmetic_stylesheet("https://shop.com/")
            .contains(".entity-ad")
    );
    assert!(
        e.cosmetic_stylesheet("https://www.shop.co.uk/")
            .contains(".entity-ad")
    );
    assert!(
        !e.cosmetic_stylesheet("https://other.com/")
            .contains(".entity-ad")
    );
}

#[test]
fn real_lists_give_safe_cosmetic_output() {
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../third_party/filter_lists");
    let (Ok(easylist), Ok(privacy)) = (
        std::fs::read(dir.join("easylist.txt")),
        std::fs::read(dir.join("easyprivacy.txt")),
    ) else {
        return; // lists are not in this checkout
    };
    let e = FilterEngine::from_lists([easylist.as_slice(), privacy.as_slice()]).unwrap();
    let mut fresh = FilterEngine::empty();
    fresh.load_serialized(&e.serialize()).unwrap();
    let page = fresh.cosmetic_stylesheet("https://news.example.test/story");
    let generic = fresh.generic_stylesheet(
        "https://news.example.test/story",
        "sponsored-ad article-body",
        "content",
    );
    assert!(
        generic.contains(".sponsored-ad{display:none!important}"),
        "{generic}"
    );
    assert!(
        !generic.contains("article-body") && !generic.contains("#content{"),
        "{generic}"
    );
    for line in page.lines().chain(generic.lines()) {
        let sel = line.strip_suffix("{display:none!important}").expect(line);
        assert!(is_safe_selector(sel), "{line}");
    }
    assert!(
        page.lines().count() < 5_000,
        "page-wide rules should stay a small set: {}",
        page.lines().count()
    );
}
