// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

use super::*;
use crate::{RequestInfo, RequestKind};
use std::time::Duration;

const BUDGET: Duration = Duration::from_secs(5);

const GOOD: &str = "\
[Adblock Plus 2.0]
! Title: Fixture list
! Version: 1
||custom-ads.test^
||tracker.test^$third-party
@@||tracker.test/ok.js
##.banner
";

#[test]
fn a_good_list_reports_its_rules_and_title() {
    let check = check_list(GOOD.as_bytes(), BUDGET).unwrap();
    assert_eq!(check.network_rules, 3);
    assert_eq!(check.title.as_deref(), Some("Fixture list"));
}

#[test]
fn a_list_with_no_network_rule_is_refused() {
    assert_eq!(
        check_list(b"! only comments\n! nothing else\n", BUDGET),
        Err(ListError::NoRules)
    );
    assert_eq!(check_list(b"", BUDGET), Err(ListError::NoRules));
    assert_eq!(
        check_list(b"##.cosmetic-only\n", BUDGET),
        Err(ListError::NoRules)
    );
}

#[test]
fn an_html_error_page_is_refused() {
    let page =
        b"<!DOCTYPE html>\n<html><body>404 Not Found</body></html>\n||looks-like-a-rule.test^\n";
    assert_eq!(check_list(page, BUDGET), Err(ListError::LooksLikeHtml));
    assert_eq!(
        check_list(b"  \n\n<html>\n||a.test^\n", BUDGET),
        Err(ListError::LooksLikeHtml)
    );
}

#[test]
fn binary_and_non_utf8_input_is_refused() {
    assert_eq!(
        check_list(b"||a.test^\n\0\n", BUDGET),
        Err(ListError::NotText)
    );
    assert_eq!(
        check_list(&[0xff, 0xfe, 0x7c, 0x7c], BUDGET),
        Err(ListError::NotText)
    );
    assert_eq!(
        check_list(&[0x1f, 0x8b, 0x08, 0x00, 0x00], BUDGET),
        Err(ListError::NotText)
    );
}

#[test]
fn an_oversize_list_is_refused_before_it_is_read() {
    let huge = vec![b'a'; MAX_LIST_BYTES + 1];
    assert_eq!(check_list(&huge, BUDGET), Err(ListError::TooLarge));
}

#[test]
fn a_spent_budget_times_the_parse_out() {
    let mut text = String::new();
    for i in 0..5000 {
        text.push_str(&format!("||host{i}.test^\n"));
    }
    assert_eq!(
        check_list(text.as_bytes(), Duration::ZERO),
        Err(ListError::Timeout)
    );
    assert!(check_list(text.as_bytes(), BUDGET).is_ok());
}

#[test]
fn over_long_lines_and_match_all_exceptions_do_not_count() {
    let mut text = String::from("||real.test^\n@@*\n@@$image\n");
    text.push_str(&"a".repeat(MAX_LINE_BYTES + 1));
    text.push('\n');
    assert_eq!(
        check_list(text.as_bytes(), BUDGET).unwrap().network_rules,
        1
    );
}

#[test]
fn building_from_checked_lists_blocks_what_each_list_says() {
    let a = "||one.test^\n";
    let b = "||two.test^\n";
    let (engine, checks) =
        FilterEngine::from_checked_lists([a.as_bytes(), b.as_bytes()], BUDGET).unwrap();
    assert_eq!(checks.len(), 2);
    for host in ["one.test", "two.test"] {
        let url = format!("https://{host}/x.js");
        let req = RequestInfo {
            url: &url,
            hostname: host,
            source_hostname: "page.test",
            kind: RequestKind::Script,
            third_party: true,
        };
        assert!(engine.should_block(&req), "{host}");
    }
}

#[test]
fn one_bad_list_stops_the_build_and_names_the_list() {
    let err = FilterEngine::from_checked_lists(
        [&b"||ok.test^\n"[..], &b"<html>"[..], &b"||ok2.test^\n"[..]],
        BUDGET,
    )
    .err()
    .unwrap();
    assert_eq!(err, (1, ListError::LooksLikeHtml));
}

#[test]
fn the_budget_is_shared_by_all_lists() {
    let err = FilterEngine::from_checked_lists([&b"||ok.test^\n"[..]], Duration::ZERO)
        .err()
        .unwrap();
    assert_eq!(err, (0, ListError::Timeout));
}
