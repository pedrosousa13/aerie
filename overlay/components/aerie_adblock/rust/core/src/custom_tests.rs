// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

use super::*;
use crate::{FilterEngine, ListError, RequestInfo, RequestKind};
use std::time::{Duration, Instant};

const BUDGET: Duration = Duration::from_secs(5);

fn analyze(text: &str) -> CustomRules {
    analyze_custom_rules(text.as_bytes(), BUDGET).unwrap()
}

fn reasons(rules: &CustomRules) -> Vec<(usize, RuleReason)> {
    rules.issues.iter().map(|i| (i.line, i.reason)).collect()
}

fn blocks(engine: &FilterEngine, host: &str, kind: RequestKind) -> bool {
    engine.should_block(&RequestInfo {
        url: &format!("https://{host}/x"),
        hostname: host,
        source_hostname: "a.localhost",
        kind,
        third_party: true,
    })
}

#[test]
fn good_rules_are_kept_and_counted() {
    let rules =
        analyze("! my rules\n||ads.test^\n\n@@||ads.test/ok.js\n##.banner\na.test##.promo\n");
    assert_eq!(rules.network_rules, 2);
    assert_eq!(rules.hide_rules, 2);
    assert!(rules.issues.is_empty(), "{:?}", rules.issues);
    assert_eq!(rules.issue_count, 0);
    for kept in [
        "! my rules",
        "||ads.test^",
        "@@||ads.test/ok.js",
        "##.banner",
        "a.test##.promo",
    ] {
        assert!(rules.text.lines().any(|l| l == kept), "{kept}");
    }
}

#[test]
fn scriptlet_rules_are_dropped_with_their_line_number() {
    let rules = analyze(
        "||ads.test^\n\
         a.test##+js(abort-on-property-read, x)\n\
         ##+js(set-constant, y, 1)\n\
         a.test#@#+js()\n\
         a.test#%#//scriptlet('abort-on-property-read', 'z')\n\
         a.test#@%#//scriptlet('x')\n",
    );
    assert_eq!(
        reasons(&rules),
        [2, 3, 4, 5, 6].map(|n| (n, RuleReason::Scriptlet))
    );
    assert!(!rules.text.contains("js("), "{}", rules.text);
    assert!(!rules.text.contains("scriptlet"), "{}", rules.text);
    assert_eq!((rules.network_rules, rules.hide_rules), (1, 0));
}

#[test]
fn procedural_html_and_style_rules_are_dropped_with_a_reason() {
    let rules = analyze(
        "a.test#?#div:-abp-has(.x)\n\
         a.test#@?#div\n\
         a.test##.x:has-text(sponsored)\n\
         a.test##^script:has-text(ad)\n\
         a.test#$#.x { color: red }\n\
         a.test#@$#.x { color: red }\n\
         a.test$$script[tag-content=\"ad\"]\n\
         a.test##.x:style(color: red)\n",
    );
    assert_eq!(
        reasons(&rules),
        [
            (1, RuleReason::Procedural),
            (2, RuleReason::Procedural),
            (3, RuleReason::Procedural),
            (4, RuleReason::HtmlFilter),
            (5, RuleReason::StyleInjection),
            (6, RuleReason::StyleInjection),
            (7, RuleReason::HtmlFilter),
            (8, RuleReason::Procedural),
        ]
    );
    assert!(rules.text.trim().is_empty(), "{}", rules.text);
}

#[test]
fn an_unsafe_selector_is_dropped() {
    let rules = analyze("a.test##.x{color:red}\na.test##a[href=\"x\"]\n");
    assert_eq!(reasons(&rules), [(1, RuleReason::UnsafeSelector)]);
    assert_eq!(rules.hide_rules, 1);
}

#[test]
fn a_rule_that_does_not_parse_is_dropped() {
    let rules = analyze("||ok.test^\n||bad.test^$no-such-option\n");
    assert_eq!(reasons(&rules), [(2, RuleReason::Unparseable)]);
    assert_eq!(rules.network_rules, 1);
}

#[test]
fn an_exception_that_matches_every_address_is_ignored_and_says_so() {
    let rules = analyze("||ads.test^\n@@http\n@@||ads.test/ok\n@@.\n");
    assert_eq!(
        reasons(&rules),
        [
            (2, RuleReason::MatchesEveryAddress),
            (4, RuleReason::MatchesEveryAddress)
        ]
    );
    assert!(RuleReason::MatchesEveryAddress.is_ignored());
    assert!(!RuleReason::Scriptlet.is_ignored());
    assert_eq!(rules.network_rules, 2);
    assert_eq!(rules.ignored_exceptions, 2);
    assert_eq!(rules.dropped, 0);
    assert!(!rules.text.contains("@@http"));
}

#[test]
fn a_line_over_four_kib_is_dropped_but_a_line_of_four_kib_is_kept() {
    let ok = format!("||{}.test^", "a".repeat(MAX_CUSTOM_LINE_BYTES - 8));
    assert_eq!(ok.len(), MAX_CUSTOM_LINE_BYTES);
    let long = format!("||{}.test^", "b".repeat(MAX_CUSTOM_LINE_BYTES));
    let rules = analyze(&format!("{ok}\n{long}\n||c.test^\n"));
    assert_eq!(reasons(&rules), [(2, RuleReason::TooLong)]);
    assert_eq!(rules.network_rules, 2);
    assert!(rules.text.contains(&ok));
    assert!(!rules.text.contains(&long));
}

#[test]
fn a_line_with_a_nul_byte_is_dropped_and_the_rest_is_kept() {
    let rules = analyze("||a.test^\n||b.te\0st^\n||c.test^\n");
    assert_eq!(reasons(&rules), [(2, RuleReason::ContainsNul)]);
    assert_eq!(rules.network_rules, 2);
    assert!(!rules.text.contains('\0'));
}

#[test]
fn the_size_and_line_caps_refuse_the_whole_text() {
    // Many short comment lines would hit the line cap first, so use wide lines for the size test.
    let wide = "!".repeat(MAX_CUSTOM_LINE_BYTES - 1) + "\n";
    let exactly = wide.repeat(MAX_CUSTOM_BYTES / wide.len());
    assert!(exactly.len() <= MAX_CUSTOM_BYTES);
    assert!(analyze_custom_rules(exactly.as_bytes(), BUDGET).is_ok());
    let over = vec![b'!'; MAX_CUSTOM_BYTES + 1];
    assert_eq!(
        analyze_custom_rules(&over, BUDGET),
        Err(CustomRulesError::TooLarge)
    );

    let lines = "!\n".repeat(MAX_CUSTOM_LINES);
    assert!(analyze_custom_rules(lines.as_bytes(), BUDGET).is_ok());
    let too_many = "!\n".repeat(MAX_CUSTOM_LINES + 1);
    assert_eq!(
        analyze_custom_rules(too_many.as_bytes(), BUDGET),
        Err(CustomRulesError::TooManyLines)
    );
    let no_final_newline = "!\n".repeat(MAX_CUSTOM_LINES) + "!";
    assert_eq!(
        analyze_custom_rules(no_final_newline.as_bytes(), BUDGET),
        Err(CustomRulesError::TooManyLines)
    );
}

#[test]
fn text_that_is_not_utf8_is_refused() {
    assert_eq!(
        analyze_custom_rules(b"||a.test^\n\xff\xfe\n", BUDGET),
        Err(CustomRulesError::NotText)
    );
}

#[test]
fn an_exhausted_budget_is_a_timeout() {
    assert_eq!(
        analyze_custom_rules(b"||a.test^\n", Duration::ZERO),
        Err(CustomRulesError::Timeout)
    );
}

#[test]
fn the_report_is_bounded_but_the_counts_are_not() {
    let text = "a.test##+js(x)\n".repeat(MAX_CUSTOM_LINES);
    let rules = analyze(&text);
    assert_eq!(rules.issues.len(), MAX_CUSTOM_ISSUES);
    assert_eq!(rules.issue_count, MAX_CUSTOM_LINES);
    assert_eq!(rules.dropped, MAX_CUSTOM_LINES);
}

#[test]
fn a_reported_rule_is_cut_on_a_character_boundary() {
    let line = format!(
        "a.test##+js({})",
        "é".repeat(MAX_CUSTOM_LINE_BYTES / 2 - 10)
    );
    let rules = analyze(&line);
    let shown = &rules.issues[0].rule;
    assert!(shown.len() <= MAX_REPORTED_RULE_BYTES);
    assert!(line.starts_with(shown.as_str()));
}

#[test]
fn hostile_input_is_handled_in_the_time_budget() {
    let started = Instant::now();
    let mut text = String::new();
    // Regex bombs, in a network rule and with options.
    text.push_str("/(a+)+$/\n/^(a|aa)+$/$script\n/(?:x*)*y/\n/[a-z]{1000}{1000}/\n/(((((((((((a+)+)+)+)+)+)+)+)+)+)+)+/\n");
    // Many exception rules, each of which is probed.
    for i in 0..3000 {
        text.push_str(&format!("@@||e{i}.test^$script\n"));
    }
    // Wide lines of junk and of symbols.
    for _ in 0..40 {
        text.push_str(&"*^|$".repeat(MAX_CUSTOM_LINE_BYTES / 4));
        text.push('\n');
    }
    text.push_str("||real.test^\n");
    let rules = analyze_custom_rules(text.as_bytes(), Duration::from_secs(20)).unwrap();
    assert!(rules.network_rules >= 1);
    let engine = FilterEngine::from_checked_lists_with(
        [(rules.text.as_bytes(), true)],
        Duration::from_secs(20),
    )
    .unwrap()
    .0;
    assert!(blocks(&engine, "real.test", RequestKind::Script));
    let long_host = format!("{}.test", "a".repeat(200));
    assert!(!blocks(&engine, &long_host, RequestKind::Script));
    assert!(
        started.elapsed() < Duration::from_secs(20),
        "{:?}",
        started.elapsed()
    );
}

#[test]
fn a_saved_rule_blocks_on_a_localhost_page_and_a_scriptlet_line_does_nothing() {
    let rules = analyze("||blocked.localhost^\na.localhost##+js(abort-on-property-read, evil)\n");
    let engine = FilterEngine::from_lists([rules.text.as_bytes()]).unwrap();
    assert!(blocks(&engine, "blocked.localhost", RequestKind::Image));
    assert!(!blocks(&engine, "other.localhost", RequestKind::Image));
    let css = engine.cosmetic_stylesheet("https://a.localhost/");
    assert!(!css.contains("evil") && !css.contains("abort"), "{css}");
    // The engine drops the line even when the text is fed to it raw: this is
    // the second wall, behind analyze_custom_rules.
    let raw = FilterEngine::from_lists([
        &b"||blocked.localhost^\na.localhost##+js(abort-on-property-read, evil)\n"[..],
    ])
    .unwrap();
    assert!(
        !raw.cosmetic_stylesheet("https://a.localhost/")
            .contains("evil")
    );
}

#[test]
fn a_list_of_hide_rules_only_builds_when_it_is_the_users_own() {
    let rules = analyze("##.banner\n");
    assert_eq!((rules.network_rules, rules.hide_rules), (0, 1));
    assert_eq!(
        FilterEngine::from_checked_lists([rules.text.as_bytes()], BUDGET).err(),
        Some((0, ListError::NoRules))
    );
    let (engine, _) =
        FilterEngine::from_checked_lists_with([(rules.text.as_bytes(), true)], BUDGET).unwrap();
    assert!(
        engine
            .generic_stylesheet("https://x.test/", "banner", "")
            .contains(".banner")
    );
    // A web page is still refused, even as the user's own list.
    assert_eq!(
        FilterEngine::from_checked_lists_with([(&b"<html>"[..], true)], BUDGET).err(),
        Some((0, ListError::LooksLikeHtml))
    );
}

#[test]
fn comments_and_blank_text_have_no_rules_and_no_issues() {
    let rules = analyze("\n\n! only a note\n   \n");
    assert_eq!(
        (rules.network_rules, rules.hide_rules, rules.issue_count),
        (0, 0, 0)
    );
}

/// Hostile and ordinary lines, one per line, to run through both paths.
fn mixed_input(with_nul: bool) -> String {
    let between = format!("||{}.test^", "m".repeat(5000)); // over 4 KiB, under 8 KiB
    let over = format!("||{}.test^", "o".repeat(9000));
    let at_limit = format!("||{}.test^", "k".repeat(MAX_CUSTOM_LINE_BYTES - 8));
    [
        "||ads.test^",
        "! comment",
        "a.test##.banner",
        "a.test##+js(abort-on-property-read, x)",
        "a.test#?#div:-abp-has(.x)",
        "a.test##.x:has-text(sponsored)",
        "a.test##^script:has-text(ad)",
        "a.test#$#.x { color: red }",
        "a.test##.x { color: red }",
        "@@http://*",
        "@@||ok.test^",
        if with_nul {
            "||nul\0byte.test^"
        } else {
            "||nonul.test^"
        },
        &between,
        &over,
        &at_limit,
        "a.test##.x:style(color: red)",
        "||last.test^",
    ]
    .join("\n")
        + "\n"
}

#[test]
fn the_report_drops_exactly_what_the_engine_path_drops() {
    let input = mixed_input(true);
    let rules = analyze(&input);
    let engine_text = crate::sanitize_list_with_line_limit(input.as_bytes(), MAX_CUSTOM_LINE_BYTES);
    let kept: Vec<&str> = engine_text.lines().collect();
    let reported: Vec<usize> = rules
        .issues
        .iter()
        .filter(|i| i.reason != RuleReason::Unparseable)
        .map(|i| i.line)
        .collect();
    for (n, line) in input.lines().enumerate() {
        let dropped_by_engine = !kept.contains(&line) || line.contains('\0');
        assert_eq!(
            reported.contains(&(n + 1)),
            dropped_by_engine && !line.trim().is_empty(),
            "line {}: {:?}",
            n + 1,
            &line[..line.len().min(60)]
        );
    }
    // The 5000-byte line is over the shared limit: the engine path drops it.
    assert!(rules.issues.iter().any(|i| i.reason == RuleReason::TooLong));
}

#[test]
fn the_engine_built_from_the_raw_text_agrees_with_the_report() {
    // A NUL anywhere makes `check_list` refuse a whole text; the report only
    // drops the line, and the saved text has no NUL left. So no NUL here.
    let input = mixed_input(false);
    let rules = analyze(&input);
    let (raw, checks) =
        FilterEngine::from_checked_lists_with([(input.as_bytes(), true)], BUDGET).unwrap();
    let from_report = FilterEngine::from_lists([rules.text.as_bytes()]).unwrap();
    // The check counts the same network rules the report counts.
    assert_eq!(checks[0].network_rules, rules.network_rules);
    for host in ["ads.test", "ok.test", "last.test", "other.test"] {
        assert_eq!(
            blocks(&raw, host, RequestKind::Image),
            blocks(&from_report, host, RequestKind::Image),
            "{host}"
        );
    }
    for host in ["m".repeat(5000), "o".repeat(9000)] {
        let host = format!("{host}.test");
        assert!(!blocks(&raw, &host, RequestKind::Image));
    }
    assert_eq!(
        raw.cosmetic_stylesheet("https://a.test/"),
        from_report.cosmetic_stylesheet("https://a.test/")
    );
}
