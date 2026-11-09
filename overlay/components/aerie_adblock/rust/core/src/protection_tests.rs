// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

use super::*;

const PHISH: &str = "\
# Title: Example list (test copy)
# Entries: 4
evil.example
Login.Bank-Secure.example.
*.wildcard.example
0.0.0.0 hosts-style.example   # a hosts-format line
127.0.0.1 localhost
bad_underscore.example
";

fn list() -> ProtectionList {
    ProtectionList::from_lists([PHISH.as_bytes()]).unwrap()
}

#[test]
fn lists_the_domain_and_every_subdomain() {
    let l = list();
    assert!(l.is_listed("evil.example"));
    assert!(l.is_listed("www.evil.example"));
    assert!(l.is_listed("a.b.c.evil.example"));
}

#[test]
fn matches_on_label_boundaries_only() {
    let l = list();
    assert!(!l.is_listed("notevil.example"));
    assert!(!l.is_listed("evil.example.attacker.test"));
    assert!(!l.is_listed("example"));
    assert!(!l.is_listed("good.example"));
}

#[test]
fn normalizes_case_trailing_dot_wildcards_and_hosts_format() {
    let l = list();
    assert!(l.is_listed("login.bank-secure.example"));
    assert!(l.is_listed("LOGIN.BANK-SECURE.EXAMPLE."));
    assert!(l.is_listed("wildcard.example"));
    assert!(l.is_listed("x.wildcard.example"));
    assert!(l.is_listed("hosts-style.example"));
    assert!(l.is_listed("bad_underscore.example"));
}

#[test]
fn skips_comments_blank_lines_and_hosts_boilerplate() {
    let l = list();
    assert_eq!(l.len(), 5);
    assert!(!l.is_listed("localhost"));
}

#[test]
fn a_single_label_entry_cannot_block_a_whole_tld() {
    let l = ProtectionList::from_lists([b"com\nco.uk\nexample\n".as_slice()]).unwrap();
    assert!(!l.is_listed("anything.com"));
    assert!(!l.is_listed("example"));
    assert!(l.is_listed("co.uk"));
    assert_eq!(l.len(), 1);
}

#[test]
fn rejects_malformed_entries() {
    for bad in [
        "1.2.3.4",
        "10.0.0.1 ",
        "has space.example",
        "-lead.example",
        "trail-.example",
        "a..b.example",
        ".leading.example",
        "ünicode.example",
        "ex!ample.test",
        "http://evil.example/path",
        "evil.example/path",
        "evil.example:8080",
        "@@||evil.example^",
        "[::1]",
        "0.0.0.0",
    ] {
        assert_eq!(parse_domain_line(bad), None, "{bad:?} must be rejected");
    }
    let long_label = format!("{}.example", "a".repeat(64));
    assert_eq!(parse_domain_line(&long_label), None);
    let long_name = format!("{}.example", vec!["abcdefghij"; 30].join("."));
    assert_eq!(parse_domain_line(&long_name), None);
}

#[test]
fn accepts_punycode_names() {
    assert_eq!(
        parse_domain_line("xn--e1afmkfd.xn--p1ai").as_deref(),
        Some("xn--e1afmkfd.xn--p1ai")
    );
}

#[test]
fn ip_literals_and_odd_hosts_never_match() {
    let l = list();
    for host in [
        "",
        ".",
        "127.0.0.1",
        "[::1]",
        "evil.example\0",
        "evil.example/x",
        " evil.example",
    ] {
        assert!(!l.is_listed(host), "{host:?}");
    }
}

#[test]
fn empty_list_lists_nothing() {
    let l = ProtectionList::empty();
    assert!(!l.is_listed("evil.example"));
    assert_eq!(l.len(), 0);
}

#[test]
fn garbage_bytes_never_panic() {
    let junk: Vec<u8> = (0..=255u8).cycle().take(100_000).collect();
    let l = ProtectionList::from_lists([junk.as_slice()]).unwrap();
    let _ = l.is_listed("evil.example");
    let mut huge_line = vec![b'a'; MAX_LINE_BYTES + 1];
    huge_line.extend_from_slice(b".example\nok.example\n");
    let l = ProtectionList::from_lists([huge_line.as_slice()]).unwrap();
    assert!(l.is_listed("ok.example"));
    assert_eq!(l.len(), 1);
}

#[test]
fn oversize_list_is_refused() {
    let huge = vec![b'a'; MAX_LIST_BYTES + 1];
    assert!(matches!(
        ProtectionList::from_lists([huge.as_slice()]),
        Err(LoadError::TooLarge)
    ));
}

#[test]
fn serialize_round_trips_and_is_sorted_and_deduplicated() {
    let l = ProtectionList::from_lists([b"b.example\na.example\nb.example\n".as_slice()]).unwrap();
    let bytes = l.serialize();
    assert!(bytes.starts_with(SERIALIZED_MAGIC));
    let text = String::from_utf8(bytes.clone()).unwrap();
    assert!(text.ends_with("a.example\nb.example\n"));
    let mut fresh = ProtectionList::empty();
    fresh.load_serialized(&bytes).unwrap();
    assert!(fresh.is_listed("x.a.example") && fresh.is_listed("b.example"));
    assert_eq!(fresh.len(), 2);
}

#[test]
fn several_lists_are_merged() {
    let l =
        ProtectionList::from_lists([b"a.example\n".as_slice(), b"0.0.0.0 b.example\n".as_slice()])
            .unwrap();
    assert!(l.is_listed("a.example") && l.is_listed("b.example"));
}

#[test]
fn load_rejects_bad_input_and_keeps_old_rules() {
    let mut l = list();
    assert_eq!(
        l.load_serialized(b"not an engine"),
        Err(LoadError::BadEngine)
    );
    assert_eq!(l.load_serialized(b""), Err(LoadError::BadEngine));
    let mut with_bad_line = SERIALIZED_MAGIC.to_vec();
    with_bad_line.extend_from_slice(b"fine.example\nhttp://evil.example/\n");
    assert_eq!(l.load_serialized(&with_bad_line), Err(LoadError::BadEngine));
    let mut not_utf8 = SERIALIZED_MAGIC.to_vec();
    not_utf8.extend_from_slice(&[0xff, 0xfe, b'\n']);
    assert_eq!(l.load_serialized(&not_utf8), Err(LoadError::BadEngine));
    assert!(
        l.is_listed("evil.example"),
        "a failed load must leave the old rules"
    );
}

#[test]
fn load_rejects_too_many_entries() {
    let mut bytes = SERIALIZED_MAGIC.to_vec();
    for i in 0..(MAX_ENTRIES + 1) {
        bytes.extend_from_slice(format!("h{i}.example\n").as_bytes());
    }
    let mut l = ProtectionList::empty();
    assert_eq!(l.load_serialized(&bytes), Err(LoadError::TooLarge));
}

#[test]
fn a_serialized_list_under_the_floor_is_rejected_and_the_old_domains_stay() {
    let mut target = list();
    let small = ProtectionList::from_lists([b"one.example\ntwo.example\n".as_slice()]).unwrap();
    let err = target.load_serialized_with_floor(&small.serialize(), 3).unwrap_err();
    assert_eq!(err, LoadError::TooFewEntries { found: 2, required: 3 });
    assert_eq!(
        err.to_string(),
        "the protection list holds 2 domain(s), at least 3 are required (empty or truncated list?)"
    );
    assert!(target.is_listed("evil.example"), "a rejected load keeps the old domains");
    assert!(!target.is_listed("one.example"));
}

#[test]
fn a_serialized_list_at_the_floor_loads() {
    let mut target = ProtectionList::empty();
    let small = ProtectionList::from_lists([b"one.example\ntwo.example\n".as_slice()]).unwrap();
    target.load_serialized_with_floor(&small.serialize(), 2).unwrap();
    assert!(target.is_listed("two.example"));
}

#[test]
fn an_empty_serialized_list_is_rejected_whatever_the_floor() {
    let mut target = ProtectionList::empty();
    let empty = ProtectionList::empty().serialize();
    assert!(target.load_serialized_with_floor(&empty, 0).is_err());
}

#[test]
fn the_shipped_floor_is_well_below_the_real_list_and_not_trivial() {
    assert!(MIN_ENTRIES >= 10_000 && MIN_ENTRIES <= 50_000);
}

#[test]
fn the_build_gn_floor_matches_the_runtime_floor() {
    let gn = include_str!("../../../BUILD.gn");
    let at = gn.find("\"--min-entries\"").expect("BUILD.gn passes --min-entries");
    let value = gn[at..].lines().nth(1).expect("a value line").trim().trim_end_matches(',').trim_matches('"');
    assert_eq!(value.parse::<usize>().unwrap(), MIN_ENTRIES);
}
