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

fn req<'a>(url: &'a str, host: &'a str, source: &'a str, kind: RequestKind, third: bool) -> RequestInfo<'a> {
    RequestInfo { url, hostname: host, source_hostname: source, kind, third_party: third }
}

#[test]
fn blocks_listed_url_with_matching_type() {
    let e = engine();
    assert!(e.should_block(&req("https://ads.example/a.js", "ads.example", "news.test", RequestKind::Script, true)));
}

#[test]
fn type_option_is_respected() {
    let e = engine();
    // `||ads.example^$script` does not cover images.
    assert!(!e.should_block(&req("https://ads.example/a.png", "ads.example", "news.test", RequestKind::Image, true)));
    assert!(e.should_block(&req("https://img-only.example/a.png", "img-only.example", "news.test", RequestKind::Image, true)));
    assert!(!e.should_block(&req("https://img-only.example/a.js", "img-only.example", "news.test", RequestKind::Script, true)));
}

#[test]
fn first_party_content_is_allowed() {
    let e = engine();
    assert!(!e.should_block(&req("https://news.test/app.js", "news.test", "news.test", RequestKind::Script, false)));
}

#[test]
fn third_party_option_uses_the_flag_the_caller_gives() {
    let e = engine();
    let url = "https://third.example/x";
    assert!(e.should_block(&req(url, "third.example", "news.test", RequestKind::Script, true)));
    assert!(!e.should_block(&req(url, "third.example", "third.example", RequestKind::Script, false)));
}

#[test]
fn exception_rules_win() {
    let e = engine();
    assert!(e.should_block(&req("https://tracker.example/t.gif", "tracker.example", "news.test", RequestKind::Image, true)));
    assert!(!e.should_block(&req("https://tracker.example/allowed/t.gif", "tracker.example", "news.test", RequestKind::Image, true)));
}

#[test]
fn important_beats_exception() {
    let e = engine();
    assert!(e.should_block(&req("https://everything.example/x", "everything.example", "news.test", RequestKind::Other, true)));
}

#[test]
fn regex_rules_work() {
    let e = engine();
    assert!(e.should_block(&req("https://cdn.test/banner-42.gif", "cdn.test", "news.test", RequestKind::Image, true)));
    assert!(!e.should_block(&req("https://cdn.test/banner-x.gif", "cdn.test", "news.test", RequestKind::Image, true)));
}

#[test]
fn empty_engine_blocks_nothing() {
    let e = FilterEngine::empty();
    assert!(!e.should_block(&req("https://ads.example/a.js", "ads.example", "news.test", RequestKind::Script, true)));
}

#[test]
fn empty_url_or_host_is_not_blocked_and_does_not_panic() {
    let e = engine();
    assert!(!e.should_block(&req("", "", "", RequestKind::Other, false)));
    assert!(!e.should_block(&req("https://ads.example/a.js", "", "", RequestKind::Script, true)));
}

#[test]
fn odd_urls_do_not_panic() {
    let e = engine();
    for url in ["data:text/plain,hi", "blob:https://a.test/uuid", "javascript:void(0)", ":", "\u{1F600}:x", "https://\u{FFFD}/"] {
        let _ = e.should_block(&req(url, "x.test", "y.test", RequestKind::Other, true));
    }
}

#[test]
fn serialize_round_trip_keeps_behaviour() {
    let bytes = engine().serialize();
    assert!(!bytes.is_empty());
    let mut loaded = FilterEngine::empty();
    loaded.load_serialized(&bytes).unwrap();
    assert!(loaded.should_block(&req("https://ads.example/a.js", "ads.example", "news.test", RequestKind::Script, true)));
    assert!(!loaded.should_block(&req("https://news.test/app.js", "news.test", "news.test", RequestKind::Script, false)));
}

#[test]
fn bad_serialized_engine_is_an_error_and_keeps_the_old_engine() {
    let mut e = engine();
    for bad in [&b""[..], b"garbage", &[0u8; 64][..], &[0xffu8; 4096][..]] {
        assert_eq!(e.load_serialized(bad), Err(LoadError::BadEngine));
    }
    // A truncated real engine fails the checksum.
    let good = engine().serialize();
    assert_eq!(e.load_serialized(&good[..good.len() / 2]), Err(LoadError::BadEngine));
    // A flipped byte fails too.
    let mut flipped = good.clone();
    let mid = flipped.len() / 2;
    flipped[mid] ^= 0x55;
    assert_eq!(e.load_serialized(&flipped), Err(LoadError::BadEngine));
    // The engine that was there before still works.
    assert!(e.should_block(&req("https://ads.example/a.js", "ads.example", "news.test", RequestKind::Script, true)));
}

#[test]
fn oversize_inputs_are_refused() {
    let huge = vec![b'a'; MAX_LIST_BYTES + 1];
    assert_eq!(FilterEngine::from_lists([&huge[..]]).err(), Some(LoadError::TooLarge));
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
    assert!(e.should_block(&req("https://ads.example/a.js", "ads.example", "n.test", RequestKind::Script, true)));
    assert!(e.should_block(&req("https://tracker.example/x", "tracker.example", "n.test", RequestKind::Other, true)));
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
    let _ = e.should_block(&req("https://a.test/x", "a.test", "b.test", RequestKind::Other, true));
}

#[test]
fn invalid_utf8_nul_and_crlf_are_handled() {
    let mut list = b"||ads.example^$script\r\n".to_vec();
    list.extend_from_slice(&[0xff, 0xfe, 0x00, b'\n']);
    list.extend_from_slice(b"||tracker.example^\r\n");
    let e = FilterEngine::from_lists([&list[..]]).unwrap();
    assert!(e.should_block(&req("https://ads.example/a.js", "ads.example", "n.test", RequestKind::Script, true)));
    assert!(e.should_block(&req("https://tracker.example/x", "tracker.example", "n.test", RequestKind::Other, true)));
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
    assert!(e.should_block(&req("https://ads.example/a.js", "ads.example", "n.test", RequestKind::Script, true)));
    assert!(e.should_block(&req("https://tracker.example/x", "tracker.example", "n.test", RequestKind::Other, true)));
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
    assert!(e.should_block(&req("https://host49999.example/a.js", "host49999.example", "n.test", RequestKind::Script, true)));
    assert!(!e.should_block(&req("https://host50000.example/a.js", "host50000.example", "n.test", RequestKind::Script, true)));
}

#[test]
fn multiple_lists_combine() {
    let a = b"||one.example^\n";
    let b = b"||two.example^\n";
    let e = FilterEngine::from_lists([&a[..], &b[..]]).unwrap();
    assert!(e.should_block(&req("https://one.example/", "one.example", "n.test", RequestKind::Other, true)));
    assert!(e.should_block(&req("https://two.example/", "two.example", "n.test", RequestKind::Other, true)));
}

#[test]
fn request_kind_names_match_the_filter_syntax() {
    use RequestKind::*;
    let names: Vec<_> = [Other, Document, Subdocument, Script, Stylesheet, Image, Font, Media, XmlHttpRequest, Ping, Websocket, Object]
        .iter()
        .map(|k| k.as_filter_type())
        .collect();
    assert_eq!(
        names,
        ["other", "document", "subdocument", "script", "stylesheet", "image", "font", "media", "xmlhttprequest", "ping", "websocket", "object"]
    );
}

#[test]
fn a_match_all_exception_line_cannot_switch_blocking_off() {
    for bad in ["@@", "@@$image", "@@*", "@@||", "@@|$script", "@@^"] {
        let list = format!("||ads.example^$script\n{bad}\n");
        let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
        assert!(
            e.should_block(&req("https://ads.example/a.js", "ads.example", "n.test", RequestKind::Script, true)),
            "{bad:?} switched blocking off"
        );
    }
    // A real exception is untouched.
    let list = "||ads.example^\n@@||ads.example/ok$script\n";
    let e = FilterEngine::from_lists([list.as_bytes()]).unwrap();
    assert!(!e.should_block(&req("https://ads.example/ok", "ads.example", "n.test", RequestKind::Script, true)));
}

#[test]
fn object_type_rules_apply_to_object_requests_only() {
    let e = FilterEngine::from_lists([&b"||plugin.example^$object\n"[..]]).unwrap();
    assert!(e.should_block(&req("https://plugin.example/x.swf", "plugin.example", "n.test", RequestKind::Object, true)));
    assert!(!e.should_block(&req("https://plugin.example/x.swf", "plugin.example", "n.test", RequestKind::Other, true)));
    assert!(!e.should_block(&req("https://plugin.example/x.js", "plugin.example", "n.test", RequestKind::Script, true)));
}
