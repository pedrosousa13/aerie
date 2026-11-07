// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

//! The Rust/C++ bridge for Aerie's Filter Engine.
//!
//! Aerie's own design, in the same spirit as Brave's `adblock_cxx` crate
//! (brave-core, MPL-2.0), without any of its code. The surface is kept as
//! small as it can be: build or load an engine, ask one yes/no question.
//!
//! Conventions that the C++ side relies on:
//!
//! * No function returns `Result`. Chromium builds C++ without exceptions, so
//!   failure is a `bool`.
//! * No function panics (see `aerie_adblock_core`).
//! * Strings passed as `&str` must be valid UTF-8. The C++ caller only passes
//!   ASCII (`GURL` specs and hosts are always ASCII).
//! * An `Engine` is not thread-safe (`adblock`'s `single-thread` feature).
//!   The C++ owner keeps it on one sequence.

use aerie_adblock_core::{FilterEngine, RequestInfo, RequestKind};

pub struct Engine(FilterEngine);

#[cxx::bridge(namespace = "aerie_adblock")]
mod ffi {
    /// Mirrors `aerie_adblock_core::RequestKind`. Keep the order in step with
    /// `kind_from_ffi` below; a test checks every variant.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    #[repr(u8)]
    enum RequestType {
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
    }

    extern "Rust" {
        type Engine;

        /// An engine with no rules. It blocks nothing.
        fn new_empty_engine() -> Box<Engine>;

        /// Compile one filter list (EasyList syntax) into a new engine. Bad
        /// lines are skipped. A list that is too large gives an empty engine.
        /// For tests, fuzzers and tools; the browser loads a serialized engine.
        fn new_engine_from_list(list: &[u8]) -> Box<Engine>;

        /// Replace the rules with a serialized engine. Returns false, and
        /// keeps the old rules, when `data` is not a valid engine.
        fn load_serialized(self: &mut Engine, data: &[u8]) -> bool;

        /// The engine in the form `load_serialized` reads. Tests and fuzzers
        /// use it; the browser only loads engines the build made.
        fn serialize(self: &Engine) -> Vec<u8>;

        /// Should this request be blocked?
        fn should_block(
            self: &Engine,
            url: &str,
            hostname: &str,
            source_hostname: &str,
            request_type: RequestType,
            third_party: bool,
        ) -> bool;

        /// The CSS to inject when the page at `page_url` commits: one
        /// `selector{display:none!important}` line per hide rule that applies
        /// to the site. Every selector passed the core's safety check. Empty
        /// for a URL that is not http(s).
        fn cosmetic_stylesheet(self: &Engine, page_url: &str) -> String;

        /// The CSS for generic hide rules that match the classes and ids a
        /// page uses (each a whitespace-separated list). Empty when the site
        /// has a `$generichide` exception.
        fn generic_stylesheet(self: &Engine, page_url: &str, classes: &str, ids: &str) -> String;
    }
}

use ffi::RequestType;

fn kind_from_ffi(t: RequestType) -> RequestKind {
    match t {
        RequestType::Document => RequestKind::Document,
        RequestType::Subdocument => RequestKind::Subdocument,
        RequestType::Script => RequestKind::Script,
        RequestType::Stylesheet => RequestKind::Stylesheet,
        RequestType::Image => RequestKind::Image,
        RequestType::Font => RequestKind::Font,
        RequestType::Media => RequestKind::Media,
        RequestType::XmlHttpRequest => RequestKind::XmlHttpRequest,
        RequestType::Ping => RequestKind::Ping,
        RequestType::Websocket => RequestKind::Websocket,
        RequestType::Object => RequestKind::Object,
        // `Other`, and any value a newer C++ side might add.
        _ => RequestKind::Other,
    }
}

fn new_empty_engine() -> Box<Engine> {
    Box::new(Engine(FilterEngine::empty()))
}

fn new_engine_from_list(list: &[u8]) -> Box<Engine> {
    let engine = FilterEngine::from_lists([list]).unwrap_or_else(|_| FilterEngine::empty());
    Box::new(Engine(engine))
}

impl Engine {
    fn load_serialized(&mut self, data: &[u8]) -> bool {
        self.0.load_serialized(data).is_ok()
    }

    fn serialize(&self) -> Vec<u8> {
        self.0.serialize()
    }

    fn cosmetic_stylesheet(&self, page_url: &str) -> String {
        self.0.cosmetic_stylesheet(page_url)
    }

    fn generic_stylesheet(&self, page_url: &str, classes: &str, ids: &str) -> String {
        self.0.generic_stylesheet(page_url, classes, ids)
    }

    fn should_block(
        &self,
        url: &str,
        hostname: &str,
        source_hostname: &str,
        request_type: RequestType,
        third_party: bool,
    ) -> bool {
        self.0.should_block(&RequestInfo {
            url,
            hostname,
            source_hostname,
            kind: kind_from_ffi(request_type),
            third_party,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ALL: [(RequestType, RequestKind); 12] = [
        (RequestType::Other, RequestKind::Other),
        (RequestType::Document, RequestKind::Document),
        (RequestType::Subdocument, RequestKind::Subdocument),
        (RequestType::Script, RequestKind::Script),
        (RequestType::Stylesheet, RequestKind::Stylesheet),
        (RequestType::Image, RequestKind::Image),
        (RequestType::Font, RequestKind::Font),
        (RequestType::Media, RequestKind::Media),
        (RequestType::XmlHttpRequest, RequestKind::XmlHttpRequest),
        (RequestType::Ping, RequestKind::Ping),
        (RequestType::Websocket, RequestKind::Websocket),
        (RequestType::Object, RequestKind::Object),
    ];

    #[test]
    fn every_request_type_maps_to_its_kind() {
        for (ffi_type, kind) in ALL {
            assert_eq!(kind_from_ffi(ffi_type), kind);
        }
    }

    #[test]
    fn bridge_blocks_and_allows() {
        let e = new_engine_from_list(b"||ads.example^$script\n@@||ads.example/ok.js\n");
        assert!(e.should_block("https://ads.example/a.js", "ads.example", "news.test", RequestType::Script, true));
        assert!(!e.should_block("https://ads.example/ok.js", "ads.example", "news.test", RequestType::Script, true));
        assert!(!e.should_block("https://ads.example/a.png", "ads.example", "news.test", RequestType::Image, true));
        assert!(!e.should_block("https://news.test/a.js", "news.test", "news.test", RequestType::Script, false));
    }

    #[test]
    fn bridge_load_serialized_reports_failure_and_keeps_rules() {
        let mut e = new_engine_from_list(b"||ads.example^\n");
        assert!(!e.load_serialized(b"not an engine"));
        assert!(e.should_block("https://ads.example/", "ads.example", "n.test", RequestType::Other, true));
        let bytes = e.serialize();
        let mut fresh = new_empty_engine();
        assert!(!fresh.should_block("https://ads.example/", "ads.example", "n.test", RequestType::Other, true));
        assert!(fresh.load_serialized(&bytes));
        assert!(fresh.should_block("https://ads.example/", "ads.example", "n.test", RequestType::Other, true));
    }

    #[test]
    fn bridge_oversize_list_gives_an_empty_engine() {
        let huge = vec![b'a'; aerie_adblock_core::MAX_LIST_BYTES + 1];
        let e = new_engine_from_list(&huge);
        assert!(!e.should_block("https://a.test/", "a.test", "b.test", RequestType::Other, true));
    }

    #[test]
    fn bridge_cosmetic_stylesheets() {
        let e = new_engine_from_list(b"news.test##.site-ad\n##.ad-banner\n##div[class*=\"Ads\"]\n");
        let page = e.cosmetic_stylesheet("https://news.test/a");
        assert!(page.contains(".site-ad{display:none!important}"), "{page}");
        assert!(page.contains("div[class*=\"Ads\"]{display:none!important}"), "{page}");
        assert!(!e.cosmetic_stylesheet("https://other.test/").contains(".site-ad"));
        assert!(e.cosmetic_stylesheet("chrome://settings/").is_empty());
        let generic = e.generic_stylesheet("https://news.test/a", "ad-banner x", "");
        assert!(generic.contains(".ad-banner{display:none!important}"), "{generic}");
        assert!(e.generic_stylesheet("https://news.test/a", "x", "y").is_empty());
    }
}
