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
# The animator APIs by name (a word such as "animated" in code, a name or a string is not one).
FORBIDDEN = re.compile(
    r"\b(LayerAnimator|GetAnimator|SetAnimator|ScopedLayerAnimationSettings|LayerAnimationSequence|LayerAnimationElement"
    r"|ImplicitAnimationObserver|AnimationBuilder|gfx::(Linear|Slide|Multi)?Animation|ui::Tween|CompositorAnimationObserver"
    r"|SetBackgroundBlur|SetLayerBlur|SetLayerFilters|SetBackdropFilter|SetBackdropFilterQuality|SetShadow|ui::Shadow"
    r"|SetRoundedCornerRadius|SetGrayscale|SetLayerBrightness|SetLayerSaturation)\b")


def strip_comments(text):
    """The code without `/* */` blocks, `//` comments and the contents of string literals."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


class FrameNotAnimatedTest(unittest.TestCase):
    def test_sources_exist(self):
        self.assertGreaterEqual(len(FRAME_SOURCES), 8)

    def test_the_checker_ignores_comments_and_strings_and_finds_real_calls(self):
        self.assertIsNone(FORBIDDEN.search(strip_comments("/* GetAnimator() */ // SetShadow\nint x;")))
        self.assertIsNone(FORBIDDEN.search(strip_comments('const char* s = "SetLayerBlur";')))
        self.assertIsNone(FORBIDDEN.search(strip_comments("int animated_count = 0;  // not an animator")))
        self.assertIsNotNone(FORBIDDEN.search(strip_comments("layer()->GetAnimator()->StopAnimating();")))
        self.assertIsNotNone(FORBIDDEN.search(strip_comments("layer()->SetLayerBlur(3);")))
        self.assertIsNotNone(FORBIDDEN.search(strip_comments("ui::ScopedLayerAnimationSettings s(a);")))

    def test_no_animation_filter_blur_or_compositor_shadow(self):
        for path in FRAME_SOURCES:
            text = path.read_text()
            # Comments say what is not done; only code counts.
            code = strip_comments(text)
            self.assertIsNone(FORBIDDEN.search(code), f"{path.name}: {FORBIDDEN.search(code)}")


if __name__ == "__main__":
    unittest.main()
