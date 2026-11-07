"""The branding layer: Aerie's placeholder art replaces Helium's, file for file."""

import os
import struct
import tempfile
import unittest
from pathlib import Path

from aerie_build import branding, layers, pipeline

REPO = Path(__file__).resolve().parent.parent
HELIUM_RES = REPO / "helium-chromium" / "resources"
OLD_NS = 1_000_000_000_000_000_000


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:24]
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG"
    return struct.unpack(">II", data[16:24])


def helium_pairs(list_name: str) -> list[list[str]]:
    rows = []
    for line in (HELIUM_RES / list_name).read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            rows.append(line.split())
    return rows


class ParseTest(unittest.TestCase):
    def test_parses_pairs_and_skips_comments_and_blank_lines(self):
        rows = branding.parse_list("# c\n\na.png b/c.png\n  d.svg  e.svg  \n")
        self.assertEqual(rows, [("a.png", "b/c.png"), ("d.svg", "e.svg")])

    def test_a_line_without_two_fields_is_an_error(self):
        for bad in ("only-one\n", "a b c\n"):
            with self.assertRaisesRegex(branding.BrandingError, "line 1"):
                branding.parse_list(bad)

    def test_unsafe_destination_is_an_error(self):
        for bad in ("a.png ../x.png\n", "a.png /abs.png\n"):
            with self.assertRaisesRegex(branding.BrandingError, "unsafe"):
                branding.parse_list(bad)

    def test_a_destination_listed_twice_is_an_error(self):
        with self.assertRaisesRegex(branding.BrandingError, "twice"):
            branding.parse_list("a.png x.png\nb.png x.png\n")


class LayerTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.art = root / "branding"
        (self.art / "art").mkdir(parents=True)
        self.tree = root / "tree"
        self.tree.mkdir()
        self.store = root / "pristine"
        self.stage = root / "stage"

    def art_file(self, name, data=b"aerie"):
        (self.art / "art" / name).write_bytes(data)

    def listing(self, text):
        (self.art / "resources.txt").write_text(text)

    def upstream(self, rel, data=b"helium"):
        path = self.tree / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        os.utime(path, ns=(OLD_NS, OLD_NS))
        return path

    def apply(self):
        return layers.apply_layers(
            self.tree, self.store, [branding.BrandingLayer(self.art)], self.stage)

    def test_replaces_the_file_helium_left(self):
        self.art_file("logo.png", b"A")
        self.listing("logo.png chrome/logo.png\n")
        self.upstream("chrome/logo.png", b"H")
        report = self.apply()
        self.assertEqual([c.kind for c in report.changes], ["modify"])
        self.assertEqual((self.tree / "chrome/logo.png").read_bytes(), b"A")

    def test_second_run_writes_nothing(self):
        self.art_file("logo.png", b"A")
        self.listing("logo.png chrome/logo.png\n")
        self.upstream("chrome/logo.png")
        self.apply()
        self.assertTrue(self.apply().idle)

    def test_one_source_can_serve_several_destinations(self):
        self.art_file("logo.png", b"A")
        self.listing("logo.png a/one.png\nlogo.png b/two.png\n")
        self.upstream("a/one.png")
        self.upstream("b/two.png")
        self.apply()
        self.assertEqual((self.tree / "b/two.png").read_bytes(), b"A")

    def test_a_destination_missing_from_the_tree_is_an_error(self):
        self.art_file("logo.png")
        self.listing("logo.png chrome/gone.png\n")
        with self.assertRaisesRegex(branding.BrandingError, "chrome/gone.png.*not in the source tree"):
            self.apply()

    def test_a_missing_art_file_is_an_error(self):
        self.listing("nope.png chrome/logo.png\n")
        self.upstream("chrome/logo.png")
        with self.assertRaisesRegex(branding.BrandingError, "nope.png"):
            self.apply()

    def test_an_empty_art_file_is_an_error(self):
        self.art_file("logo.png", b"")
        self.listing("logo.png chrome/logo.png\n")
        self.upstream("chrome/logo.png")
        with self.assertRaisesRegex(branding.BrandingError, "empty"):
            self.apply()

    def test_removing_an_entry_restores_helium_art(self):
        self.art_file("logo.png", b"A")
        self.listing("logo.png chrome/logo.png\n")
        self.upstream("chrome/logo.png", b"H")
        self.apply()
        self.listing("")
        self.apply()
        self.assertEqual((self.tree / "chrome/logo.png").read_bytes(), b"H")

    def test_no_branding_directory_is_no_branding(self):
        report = layers.apply_layers(
            self.tree, self.store, [branding.BrandingLayer(self.art / "missing")], self.stage)
        self.assertTrue(report.idle)


class PipelineTest(unittest.TestCase):
    def test_the_branding_layer_runs_between_the_overlay_and_the_substitution(self):
        ctx = type("C", (), {"repo": REPO, "tree": Path("/t"), "core": REPO / "helium-chromium"})()
        names = [layer.name for layer in pipeline.aerie_layers(ctx, lambda _m: None)]
        self.assertEqual(names.index("branding") - 1, names.index("overlay"))
        self.assertLess(names.index("branding"), names.index("helium-to-aerie substitution"))


class RepoArtTest(unittest.TestCase):
    """The committed list and art, against Helium's own resource lists."""

    @classmethod
    def setUpClass(cls):
        cls.rows = branding.parse_list(branding.list_path(REPO / "branding").read_text())

    def test_every_listed_art_file_exists_and_is_not_empty(self):
        for src, _ in self.rows:
            path = REPO / "branding" / "art" / src
            self.assertTrue(path.is_file(), src)
            self.assertGreater(path.stat().st_size, 0, src)

    def test_every_art_file_is_listed(self):
        listed = {src for src, _ in self.rows}
        on_disk = {p.relative_to(REPO / "branding" / "art").as_posix()
                   for p in (REPO / "branding" / "art").rglob("*") if p.is_file()}
        self.assertEqual(on_disk - listed, set())

    def helium_logo_destinations(self) -> dict[str, tuple[int, int] | None]:
        """Destinations where Helium puts its logo, with the size of Helium's image (None: not a PNG)."""
        generated = {r[2]: int(r[1]) for r in helium_pairs("generate_resources.txt")}
        sizes: dict[str, tuple[int, int] | None] = {}
        for src, dest in helium_pairs("helium_resources.txt"):
            if src.startswith("generated/"):
                sizes[dest] = (generated[src], generated[src])
            elif src.startswith(("branding/", "favicons/favicon_ntp_")):
                sizes[dest] = png_size(HELIUM_RES / src) if src.endswith(".png") else None
        return sizes

    def test_every_place_helium_puts_its_logo_is_replaced(self):
        if not (HELIUM_RES / "helium_resources.txt").exists():
            self.skipTest("helium-chromium submodule not checked out")
        dests = {dest for _, dest in self.rows}
        missing = sorted(set(self.helium_logo_destinations()) - dests)
        self.assertEqual(missing, [], "Helium art still ships at these paths")

    def test_png_sizes_equal_the_ones_helium_ships(self):
        if not (HELIUM_RES / "helium_resources.txt").exists():
            self.skipTest("helium-chromium submodule not checked out")
        expected = self.helium_logo_destinations()
        for src, dest in self.rows:
            want = expected.get(dest)
            if want is not None and src.endswith(".png"):
                self.assertEqual(png_size(REPO / "branding" / "art" / src), want, dest)

    def test_vector_art_does_not_say_helium(self):
        for src, _ in self.rows:
            if src.endswith((".svg", ".icon")):
                text = (REPO / "branding" / "art" / src).read_text()
                self.assertNotIn("Helium", text, src)
                self.assertNotIn("helium", text.lower(), src)

    def test_no_destination_is_outside_art_folders(self):
        for _, dest in self.rows:
            self.assertRegex(dest, r"\.(png|svg|icon)$")


if __name__ == "__main__":
    unittest.main()
