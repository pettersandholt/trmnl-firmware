#!/usr/bin/env python3
"""Host regression for the actual menu item mapping in local and upstream builds."""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / "src/menu.cpp").read_text()
    declarations = source[source.index("enum main_menu_item_t {"):source.index("// The Special submenu")]
    start = source.index("  const char *main_labels[MAIN_MENU_COUNT];")
    mapping = source[start:source.index("  menu_screen_t screen =", start)]

    # Both direct-drawing entry points must invalidate the cached diff baseline.
    for entry in ("menu_show_screen", "tutorial_show_screen"):
        start = source.index(f"static void {entry}(")
        drawing = source[start:source.index("\n}", start)]
        assert drawing.index("DisplayedImage::cover();") < drawing.index("bbep.fullUpdate(")

    with tempfile.TemporaryDirectory(prefix="trmnl-menu-test-") as directory:
        temp = Path(directory)
        for local in (False, True):
            cases = []
            for tutorial in (False, True):
                items = ["NEXT_SCREEN", "PREVIOUS_SCREEN"]
                labels = ["Next Screen", "Previous Screen"]
                if tutorial:
                    items.append("TUTORIAL")
                    labels.append("Tutorial")
                if not local:
                    items.append("SPECIAL")
                    labels.append("Special...")
                items.extend(("SETTINGS", "CLOSE"))
                labels.extend(("Settings...", "Close"))
                cases.append("""
                {
                    struct Tutorial { const char *item; } tutorial = {"Tutorial"};
                    struct Controls { const Tutorial *tutorial; } controls = {%s ? &tutorial : nullptr};
                    const Controls *menu_controls = &controls;
                    %s
                    const main_menu_item_t expected[] = {%s};
                    const char *expected_labels[] = {%s};
                    assert(main_count == %d);
                    for (int i = 0; i < main_count; ++i) {
                        assert(main_items[i] == expected[i]);
                        assert(std::strcmp(main_labels[i], expected_labels[i]) == 0);
                    }
                }
                """ % (
                    "true" if tutorial else "false", mapping,
                    ", ".join("MAIN_MENU_" + item for item in items),
                    ", ".join('"' + label + '"' for label in labels), len(items),
                ))
            (temp / "test.cpp").write_text(
                "#include <cassert>\n#include <cstring>\n" + declarations
                + "int main() {\n" + "\n".join(cases) + "\n}\n"
            )
            binary = temp / "test"
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
                *(["-DTRMNL_X_LOCAL_MENU_ONLY"] if local else []),
                str(temp / "test.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
            print(f"{'Local' if local else 'Upstream'} menu: PASS (with/without tutorial)")
    print("Menu/tutorial baseline invalidation: PASS (source ordering)")


if __name__ == "__main__":
    main()
