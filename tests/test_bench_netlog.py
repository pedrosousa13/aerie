import json
import tempfile
import unittest
from pathlib import Path

from aerie_bench import netlog

CONSTS = {"logEventTypes": {"URL_REQUEST_START_JOB": 7, "REQUEST_ALIVE": 8, "OTHER": 9},
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


if __name__ == "__main__":
    unittest.main()
