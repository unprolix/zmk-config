#!/usr/bin/env python3
"""
Redraw the per-layer pictures embedded in cheatsheet.html.

Every layer is drawn twice, Rolio first and then the eyelash corne, straight
from each board's keymap with keymap-drawer, and embedded as a data URI so the
page stays one printable file. Run it from the dev shell: `just cheatsheet`.

Both boards parse through draw/config.yaml. The Rolio gets a copy with only
the key-label header swapped, so the two pictures cannot end up labelled
differently. Dark mode is switched off in the copies rather than stripped out
of the SVG afterwards: this is a sheet for printing.
"""

import base64
import pathlib
import re
import subprocess
import tempfile

import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAGE = ROOT / "cheatsheet.html"
CONFIG = ROOT / "draw" / "config.yaml"

EYELASH_HEADER = "#include <zmk-helpers/key-labels/eyelash42.h>"
DARK_MODE_SETTING = "dark_mode: auto"

# (caption, keymap, physical layout, key-label header)
BOARDS = [
    ("Rolio",
     "config/rolio.keymap",
     "config/boards/shields/rolio/rolio-layout.dtsi",
     '#include "rolio48.h"'),
    ("Eyelash corne",
     "config/eyelash_corne.keymap",
     "zmk-new_corne/boards/shields/eyelash_corne/eyelash_corne-layouts.dtsi",
     EYELASH_HEADER),
]

# Cheatsheet <h3> -> the layer's name as keymap-drawer reads its display-name.
LAYERS = {
    "Hierophant (base)": "hierophant",
    "QWERTY": "qwerty",
    "Symbol": "symbol",
    "Numpad": "numpad",
    "Numeric": "numeric",
    "Navigation": "navigation",
    "LGUI + nav": "LGUI+nav",
    "RGUI + nav": "RGUI+nav",
    "Extra": "extra",
    "Function": "function",
    "Superscript": "superscript",
    "Empty 2": "empty 2",
    "Gaming": "gaming",
    "Lighting": "lighting",
    "Bluetooth": "bluetooth",
    "System": "system",
    "All combos": "Combos",
}

COMBOS_LAYER = "Combos"


def board_config(header: str, workdir: pathlib.Path) -> pathlib.Path:
    text = CONFIG.read_text()
    if EYELASH_HEADER not in text or DARK_MODE_SETTING not in text:
        raise SystemExit(f"{CONFIG} no longer has the lines this script rewrites")
    text = text.replace(EYELASH_HEADER, header).replace(DARK_MODE_SETTING, "dark_mode: false")
    path = workdir / f"config-{abs(hash(header))}.yaml"
    path.write_text(text)
    return path


def parse(config: pathlib.Path, keymap: str, workdir: pathlib.Path) -> pathlib.Path:
    raw = subprocess.run(
        ["keymap", "-c", str(config), "parse", "-z", str(ROOT / keymap),
         "--virtual-layers", COMBOS_LAYER],
        check=True, capture_output=True, text=True, cwd=ROOT).stdout
    data = yaml.safe_load(raw)
    # As `just draw` does: combos go only on their own picture, so the layer
    # pictures show keys and nothing else.
    for combo in data.get("combos", []):
        combo["l"] = [COMBOS_LAYER]
    path = workdir / (pathlib.Path(keymap).stem + ".yaml")
    path.write_text(yaml.safe_dump(data, allow_unicode=True, sort_keys=False))
    return path


def draw(config: pathlib.Path, parsed: pathlib.Path, layout: str, layer: str) -> str:
    svg = subprocess.run(
        ["keymap", "-c", str(config), "draw", str(parsed),
         "-d", str(ROOT / layout), "-s", layer],
        check=True, capture_output=True, text=True, cwd=ROOT).stdout
    return base64.b64encode(svg.encode()).decode()


def figure(caption: str, title: str, svg_b64: str) -> str:
    return (f'<figure class="board"><figcaption>{caption}</figcaption>'
            f'<img class="layermap" alt="{title} layer on the {caption}" '
            f'src="data:image/svg+xml;base64,{svg_b64}"></figure>')


def main() -> None:
    page = PAGE.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        workdir = pathlib.Path(tmp)
        prepared = []
        for caption, keymap, layout, header in BOARDS:
            config = board_config(header, workdir)
            prepared.append((caption, layout, config, parse(config, keymap, workdir)))

        for title, layer in LAYERS.items():
            pictures = "\n".join(
                figure(caption, title, draw(config, parsed, layout, layer))
                for caption, layout, config, parsed in prepared)
            block = re.compile(
                r'(<div class="layer">\s*<h3>' + re.escape(title) + r'</h3>\s*'
                r'<p class="small">.*?</p>)'
                r'(?:\s*(?:<figure class="board">.*?</figure>|<img class="layermap"[^>]*>))+'
                r'(\s*</div>)', re.S)
            page, count = block.subn(
                lambda m: m.group(1) + "\n" + pictures + m.group(2), page)
            if count != 1:
                raise SystemExit(f"expected one layer block titled {title!r}, found {count}")

    PAGE.write_text(page)
    print(f"redrew {len(LAYERS)} layers for {len(BOARDS)} boards")


if __name__ == "__main__":
    main()
