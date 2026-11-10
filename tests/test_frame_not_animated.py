"""Nothing about the window frame is animated (issue #178).

The browser test asserts no animation is running on the tint layer or in the sidebar. This test is the other
half: the frame's sources never ask for one, so no animator, filter, blur or shadow is ever set on the tint
layer. The shadow of the page card is painted into the layer's bitmap, once per state change; it is not a
compositor effect that could animate.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
FRAME_SOURCES = [
    *(ROOT / "overlay/components/aerie_frame").glob("*.cc"),
    *(ROOT / "overlay/components/aerie_frame").glob("*.h"),
    *(ROOT / "overlay/chrome/browser/ui/views/aerie_frame").glob("frame_tint_*"),
]
FORBIDDEN = re.compile(r"Animat|SetBackgroundBlur|SetLayerBlur|SetLayerFilters|SetBackdropFilter|SetShadow|ui::Shadow|AnimateLayer")


class FrameNotAnimatedTest(unittest.TestCase):
    def test_sources_exist(self):
        self.assertGreaterEqual(len(FRAME_SOURCES), 8)

    def test_no_animation_filter_blur_or_compositor_shadow(self):
        for path in FRAME_SOURCES:
            text = path.read_text()
            # Comments say what is not done; only code counts.
            code = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("//"))
            self.assertIsNone(FORBIDDEN.search(code), f"{path.name}: {FORBIDDEN.search(code)}")


if __name__ == "__main__":
    unittest.main()
