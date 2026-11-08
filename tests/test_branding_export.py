import importlib.util
import tempfile
import unittest
from pathlib import Path

from aerie_branding import export

REPO = Path(__file__).resolve().parent.parent
HAVE_EXPORT_DEPS = all(importlib.util.find_spec(m) for m in ("PIL", "pathops"))


class MasterTest(unittest.TestCase):
    """The master SVGs are the source of the art, so they must parse as the exporter expects."""

    def test_both_masters_parse(self):
        for name in ("soar-mark.svg", "soar-mark-small.svg"):
            with self.subTest(name=name):
                m = export.load_master(name)
                self.assertEqual(len(m.cubics), 2)
                self.assertGreater(m.stroke_width, 0)

    def test_small_master_is_heavier(self):
        self.assertGreater(export.load_master("soar-mark-small.svg").stroke_width,
                           export.load_master("soar-mark.svg").stroke_width)

    def _svg(self, d="M0 0 C1 1 2 2 3 3", view="0 0 256 256", cap="round"):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view}">'
                f'<path d="{d}" stroke-width="10" stroke-linecap="{cap}" stroke-linejoin="round"/>'
                '<circle cx="1" cy="2" r="3"/></svg>')

    def test_rejects_what_it_cannot_draw(self):
        for bad in (self._svg(view="0 0 100 100"), self._svg(d="m0 0 c1 1 2 2 3 3"),
                    self._svg(d="M0 0 L5 5"), self._svg(cap="butt"), self._svg(d="M0 0 C1 1 2 2"),
                    self._svg(d="M0 0 C1 1 2 2 3 3 S1 1 2 2 3 3")):
            with self.subTest(svg=bad), self.assertRaises(export.MasterError):
                export.parse_master(bad)

    def test_missing_numbers_are_a_master_error(self):
        for drop in ('stroke-width="10"', 'cx="1"', 'r="3"'):
            with self.subTest(drop=drop), self.assertRaises(export.MasterError):
                export.parse_master(self._svg().replace(drop, ""))

    def test_parses_points(self):
        m = export.parse_master(self._svg(d="M1 2 C3 4 5 6 7 8"))
        self.assertEqual(m.start, (1, 2))
        self.assertEqual(m.cubics, (((3, 4), (5, 6), (7, 8)),))
        self.assertEqual(m.sun, (1, 2, 3))


class ArtworkListTest(unittest.TestCase):
    def test_covers_every_art_file_the_branding_layer_maps(self):
        listing = REPO / "branding" / "resources.txt"
        named = {line.split()[0] for line in listing.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.lstrip().startswith("#")}
        self.assertEqual(named - set(export.ARTWORK), set())


@unittest.skipUnless(HAVE_EXPORT_DEPS, "needs Pillow and skia-pathops (see tools/aerie_branding/export.py)")
class ExportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls._tmp.name)
        cls.made = export.export(cls.out)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_writes_every_file_at_its_size(self):
        from PIL import Image

        self.assertEqual(set(self.made), set(export.ARTWORK))
        for name, size in export.ARTWORK.items():
            with self.subTest(name=name):
                path = self.out / name
                self.assertTrue(path.is_file())
                if size:
                    with Image.open(path) as im:
                        self.assertEqual(im.size, size)
                        self.assertEqual(im.mode, "RGBA")

    def test_text_files_match_the_committed_art(self):
        for name, size in export.ARTWORK.items():
            if size is None:
                with self.subTest(name=name):
                    self.assertEqual((self.out / name).read_bytes(),
                                     (REPO / "branding" / "art" / name).read_bytes())

    def test_vector_icons_use_known_commands(self):
        known = {"CANVAS_DIMENSIONS", "FILL_RULE_NONZERO", "NEW_PATH", "PATH_COLOR_ARGB",
                 "ROUND_RECT", "MOVE_TO", "LINE_TO", "CUBIC_TO", "CLOSE"}
        for name in ("product_logo.icon", "product_logo_color.icon"):
            with self.subTest(name=name):
                lines = (self.out / name).read_text(encoding="utf-8").splitlines()
                commands = {l.split(",")[0] for l in lines if l and not l.startswith("//")}
                self.assertLessEqual(commands, known)
                self.assertIn("CLOSE", commands)

    def test_icon_corners_are_transparent_and_centre_is_white(self):
        from PIL import Image

        with Image.open(self.out / "app_icon/256.png") as im:
            self.assertEqual(im.getpixel((0, 0))[3], 0)
            self.assertEqual(im.getpixel((128, 160))[:3], (255, 255, 255))  # the sun


if __name__ == "__main__":
    unittest.main()
