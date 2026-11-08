import json
import tempfile
import unittest
from pathlib import Path

from aerie_bench import netlog

CONSTS = {"logEventTypes": {"URL_REQUEST_REDIRECTED": 10, "URL_REQUEST_START_JOB": 7, "REQUEST_ALIVE": 8, "OTHER": 9},
          "logSourceType": {"URL_REQUEST": 1, "SOCKET": 2}}


def ev(i, url, initiator=None, type_=7, src=1, **extra):
    params = {"url": url, "method": "GET", **extra}
    if initiator:
        params["initiator"] = initiator
    return {"phase": 1, "source": {"id": i, "type": src}, "time": str(i * 10), "type": type_, "params": params}


EVENTS = [
    ev(1, "http://127.0.0.1:5000/index.html"),
    ev(2, "http://127.0.0.1:5000/style.css", initiator="http://127.0.0.1:5000"),
    ev(3, "https://update.example.invalid/check", initiator="not an origin"),
    {"phase": 2, "source": {"id": 3, "type": 1}, "time": "31", "type": 8, "params": {"net_error": -105}},
    ev(4, "tcp://x", src=2),
    ev(5, "https://chrome.example.invalid/x", type_=9),
]


def doc(events):
    return json.dumps({"constants": CONSTS, "events": events})


class RedirectHopsTest(unittest.TestCase):
    """A request that redirects is one source with several START_JOB events."""

    HOPS = [
        ev(1, "http://127.0.0.1:5000/go"),
        {"phase": 0, "source": {"id": 1, "type": 1}, "time": "11", "type": 10,
         "params": {"location": "https://www.google-analytics.com/collect?v=1"}},
        ev(1, "https://www.google-analytics.com/collect?v=1"),
        ev(2, "http://127.0.0.1:5000/plain"),
    ]

    def test_every_hop_url_is_recorded_in_order(self):
        recs = {r["source_id"]: r for r in netlog.parse(doc(self.HOPS))}
        self.assertEqual(recs[1]["urls"], ["http://127.0.0.1:5000/go",
                                           "https://www.google-analytics.com/collect?v=1"])
        self.assertEqual(recs[1]["url"], "http://127.0.0.1:5000/go")  # the first, as before
        self.assertEqual(recs[2]["urls"], ["http://127.0.0.1:5000/plain"])

    def test_a_redirect_location_without_a_start_job_is_still_seen(self):
        # The hop was announced but the job event was lost (a truncated log).
        events = [self.HOPS[0], self.HOPS[1]]
        recs = netlog.parse(doc(events))
        self.assertIn("https://www.google-analytics.com/collect?v=1", recs[0]["urls"])

    def test_an_external_hop_makes_the_request_external(self):
        recs = netlog.parse(doc(self.HOPS))
        self.assertEqual([r["source_id"] for r in netlog.external(recs)], [1])
        self.assertTrue(recs[0]["external"])

    def test_duplicates_are_not_repeated(self):
        events = self.HOPS[:3]  # location, then the job for the same URL
        self.assertEqual(len(netlog.parse(doc(events))[0]["urls"]), 2)

    def test_all_urls_helper(self):
        recs = netlog.parse(doc(self.HOPS))
        self.assertEqual(netlog.all_urls(recs), {"http://127.0.0.1:5000/go", "http://127.0.0.1:5000/plain",
                                                 "https://www.google-analytics.com/collect?v=1"})


class ParseTest(unittest.TestCase):
    def test_extracts_url_requests_only_once_each(self):
        recs = netlog.parse(doc(EVENTS))
        self.assertEqual([r["url"] for r in recs], [
            "http://127.0.0.1:5000/index.html", "http://127.0.0.1:5000/style.css",
            "https://update.example.invalid/check"])

    def test_initiator_and_external_and_error(self):
        recs = {r["source_id"]: r for r in netlog.parse(doc(EVENTS))}
        self.assertIsNone(recs[1]["initiator"])
        self.assertEqual(recs[2]["initiator"], "http://127.0.0.1:5000")
        self.assertIsNone(recs[3]["initiator"])
        self.assertEqual([r["source_id"] for r in netlog.external(list(recs.values()))], [3])
        self.assertEqual(recs[3]["net_error"], -105)
        self.assertEqual(recs[3]["host"], "update.example.invalid")

    def test_bench_fixture_names_are_loopback_not_external(self):
        urls = ["http://a.localhost:5/x", "http://plain.bench.test:5/", "http://localhost:5/",
                "http://[::1]:5/", "http://127.0.0.2:5/", "https://update.example.invalid/check",
                "http://notlocalhost.example/", "http://localhost.evil.example/"]
        evs = [ev(i, u) for i, u in enumerate(urls, 1)]
        recs = netlog.parse(doc(evs))
        self.assertEqual([r["external"] for r in recs],
                         [False, False, False, False, False, True, True, True])

    def test_truncated_file_keeps_complete_events(self):
        full = '{"constants":' + json.dumps(CONSTS) + ',\n"events": [\n' + ",\n".join(json.dumps(e) for e in EVENTS)
        truncated = full[: full.rindex(",\n")] + ',\n{"phase":1,"source":{"id":9,'
        recs = netlog.parse(truncated)
        self.assertEqual(len(recs), 3)

    def test_empty_and_garbage(self):
        self.assertEqual(netlog.parse(""), [])
        self.assertEqual(netlog.parse("not json"), [])

    def test_parse_file_missing_is_empty(self):
        self.assertEqual(netlog.parse_file("/nonexistent/netlog.json"), [])
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "n.json"
            p.write_text(doc(EVENTS))
            self.assertEqual(len(netlog.parse_file(p)), 3)


DNS_CONSTS = {"logEventTypes": {"URL_REQUEST_START_JOB": 7, "HOST_RESOLVER_MANAGER_REQUEST": 20,
                                "HOST_RESOLVER_DNS_TASK": 21, "SOCKET_CONNECT": 22, "OTHER": 23},
              "logSourceType": {"URL_REQUEST": 1, "HOST_RESOLVER_IMPL_JOB": 3, "CONNECT_JOB": 4, "SOMETHING": 9}}


def dns(i, type_, src, **params):
    return {"phase": 1, "source": {"id": i, "type": src}, "time": str(i), "type": type_, "params": params}


class LoadTest(unittest.TestCase):
    def doc(self, events):
        return json.dumps({"constants": DNS_CONSTS, "events": events})

    def test_complete_file_is_flagged_complete(self):
        self.assertTrue(netlog.load(self.doc([])).complete)

    def test_truncated_and_garbage_files_are_incomplete(self):
        text = self.doc([ev(1, "http://127.0.0.1/")])
        self.assertFalse(netlog.load(text[:-5]).complete)
        self.assertFalse(netlog.load("").complete)
        self.assertFalse(netlog.load("not json").complete)
        self.assertFalse(netlog.load_file("/nonexistent/netlog.json").complete)

    def test_host_names_come_from_resolver_connect_and_socket_events(self):
        events = [
            dns(1, 20, 3, host="www.google.com:443"),
            dns(2, 21, 3, host="https://dns.google"),
            dns(3, 22, 4, group_id="https://update.googleapis.com:443"),
            dns(4, 20, 3, host="WWW.Google.com:443"),          # same host again: listed once
            dns(5, 23, 9, host="not-a-lookup.example"),        # not a lookup kind
            dns(6, 20, 3, host="[2001:db8::1]:443"),
        ]
        got = netlog.load(self.doc(events)).lookups
        self.assertEqual([l["host"] for l in got],
                         ["www.google.com", "dns.google", "update.googleapis.com", "2001:db8::1"])
        self.assertEqual(got[0]["source_type"], "HOST_RESOLVER_IMPL_JOB")
        self.assertEqual(got[0]["event"], "HOST_RESOLVER_MANAGER_REQUEST")

    def test_url_records_are_unchanged(self):
        recs = netlog.load(self.doc([ev(1, "https://a.example/x")])).records
        self.assertEqual([r["host"] for r in recs], ["a.example"])


if __name__ == "__main__":
    unittest.main()


class SocketIdsTest(unittest.TestCase):
    """Which sockets carried each URL request (issue #73: no connection is reused across Workspaces)."""

    CONSTS = {"logEventTypes": {"URL_REQUEST_START_JOB": 1, "HTTP_STREAM_REQUEST_BOUND_TO_JOB": 2,
                                "SOCKET_POOL_BOUND_TO_SOCKET": 3, "HTTP_STREAM_JOB_CONTROLLER_BOUND": 4,
                                "HTTP_STREAM_JOB_BOUND_TO_REQUEST": 5, "SOCKET_IN_USE": 6},
              "logSourceType": {"URL_REQUEST": 1, "HTTP_STREAM_JOB": 2, "SOCKET": 3,
                                "HTTP_STREAM_JOB_CONTROLLER": 4}}

    @staticmethod
    def dep(src, src_type, kind, dep_id, dep_type):
        return {"phase": 0, "source": {"id": src, "type": src_type}, "time": "1", "type": kind,
                "params": {"source_dependency": {"id": dep_id, "type": dep_type}}}

    def request(self, rid, job, sock, url, controller=None):
        d = self.dep
        out = [{"phase": 1, "source": {"id": rid, "type": 1}, "time": "1", "type": 1,
                "params": {"url": url, "method": "GET"}}]
        if controller:
            out.append(d(rid, 1, 4, controller, 4))
        out += [d(rid, 1, 2, job, 2), d(job, 2, 3, sock, 3), d(sock, 3, 6, job, 2),
                d(job, 2, 5, rid, 1)]
        return out

    def parse(self, events):
        return netlog.parse(json.dumps({"constants": self.CONSTS, "events": events}))

    def test_a_request_lists_the_socket_its_stream_job_bound_to(self):
        recs = self.parse(self.request(10, 11, 12, "http://localhost:1/ws/ping?w=A", controller=13))
        self.assertEqual(recs[0]["sockets"], [12])

    def test_two_requests_on_one_socket_share_its_id_and_a_third_does_not(self):
        events = (self.request(10, 11, 50, "http://localhost:1/a") + self.request(20, 21, 50, "http://localhost:1/b")
                  + self.request(30, 31, 60, "http://localhost:1/c"))
        recs = {r["url"][-1]: r["sockets"] for r in self.parse(events)}
        self.assertEqual(recs, {"a": [50], "b": [50], "c": [60]})

    def test_the_walk_does_not_leak_through_a_socket_to_another_request(self):
        # SOCKET_IN_USE points socket -> job; the socket must not lead to the other request's job.
        events = self.request(10, 11, 50, "http://localhost:1/a") + self.request(20, 21, 50, "http://localhost:1/b")
        recs = self.parse(events)
        self.assertEqual([r["sockets"] for r in recs], [[50], [50]])

    def test_a_request_with_no_connection_has_no_sockets(self):
        recs = self.parse([{"phase": 1, "source": {"id": 1, "type": 1}, "time": "1", "type": 1,
                            "params": {"url": "http://localhost:1/x", "method": "GET"}}])
        self.assertEqual(recs[0]["sockets"], [])
