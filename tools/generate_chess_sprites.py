#!/usr/bin/env python3
"""Generate 60x60 1-bit chess piece sprites.

Outputs:
- assets/sprites/*.bin (12 files)
- src/EmbeddedChessSprites.h/.cpp (embedded defaults)

Sprite packing matches src/ChessPuzzlesApp.cpp renderPiece:
- Row-major bitIndex = y*60 + x
- byteIndex = bitIndex//8
- bitOffset = bitIndex%8 (LSB-first)

This script uses chess-for-kindle SVG piece paths (MIT-licensed repo):
https://github.com/artemartemenko/chess-for-kindle
"""

from __future__ import annotations

import os
import textwrap
import xml.etree.ElementTree as ET

from cairosvg import svg2png
from PIL import Image


PIECE_SIZE = 60
PIECE_BYTES = (PIECE_SIZE * PIECE_SIZE + 7) // 8
RENDER_SIZE = 240
PADDING = 4


RAW_SVGS: dict[str, str] = {
    "P": r'''<svg viewBox="-10 -10 310.789 389.771"><path style="fill:#454242" stroke="#fff" stroke-width="10" d="M289.604 369.771H.808c-3.901-32.071 7.231-51.733 13.937-63.617 8.371-14.712 18.978-22.513 31.295-31.523 11.085-8.143 23.677-17.381 36.017-33.28 13.002-16.788 22.285-36.428 27.623-58.371l-3.33-4.22H71.291a7.736 7.736 0 0 1-7.505-5.862l-3.809-15.283c-.73-4.676 1.574-9.306 5.771-11.565l35.173-19.115.57-5.634c-15.488-13.07-24.361-32.23-24.361-52.508C77.13 30.862 107.992 0 145.925 0c37.933 0 68.795 30.862 68.795 68.795 0 21.008-9.398 40.579-25.798 53.695l.593 5.725 32.869 16.719c4.676 2.372 7.117 7.778 5.771 12.933l-3.627 14.986c-.821 3.467-3.923 5.908-7.505 5.908h-30.725l-3.376 3.946c3.262 20.643 10.378 39.119 21.145 54.972 12.637 18.613 27.212 28.832 40.054 37.842 12.979 9.101 25.205 17.678 33.713 33.485 6.182 11.542 16.446 30.611 11.77 60.765z"/></svg>''',
    "N": r'''<svg viewBox="-10 -10 386.486 461.672"><path style="fill:#454242" stroke="#fff" stroke-width="10" d="M108.666 316.423c12.568-20.506 29.448-34.26 45.825-47.581 11.975-9.74 23.289-18.932 32.048-30.223 12.386-15.921 11.542-33.667 6.615-44.981a62.62 62.62 0 0 1-.798-1.962c-.775-2.03-1.209-2.714-3.239-4.425a4.996 4.996 0 0 0-3.125-1.118 4.931 4.931 0 0 0-3.513 1.506c-3.764 3.992-37.294 39.073-54.196 43.293-6.455 1.597-11.838 1.391-16.583 1.186-8.622-.342-16.081-.639-22.696 10.972-6.318 11.04-13.481 18.522-17.176 22.012a8.609 8.609 0 0 1-5.109 2.327c-27.303 2.6-45.916-10.014-57.549-19.731-12.91-10.743-8.576-24.475-8.052-25.958 1.437-3.467 27.805-50.592 39.142-57.093 13.572-7.733 31.774-65.077 33.736-74.931 2.098-10.47 19.936-25.843 31.774-36.04l1.255-1.095c6.752-5.839 7.322-12.933 7.892-19.845.661-7.961 1.346-16.172 9.808-25.798 3.148-3.604 7.824-7.687 12.158-6.82 5.201 1.049 8.052 8.508 8.348 9.352l.251.707 28.261 28.763c2.943 2.965 6.797 4.813 10.835 5.201 23.266 2.395 61.929 16.492 90.783 38.8 37.18 28.741 59.762 66.97 69.046 116.832 9.557 51.414-11.108 128.534-17.267 149.702-123.721-18.362-212.679-5.36-233.071-1.802 1.846-10.174 10.788-25.457 14.597-31.25z"/><path style="fill:#454242" stroke="#fff" stroke-width="10" d="M366.486 433.278c0 4.63-3.764 8.394-8.394 8.394H56.819c-4.63 0-8.394-3.764-8.394-8.394v-23.973c0-25.821 17.541-48.083 42.677-54.128 2.692-.547 98.037-19.685 237.611 1.437 22.605 7.687 37.773 28.832 37.773 52.691v23.973z"/><path style="fill:#fff" d="M122.43 102.104s-8.541 10.677-11.259 23.489c0 0 7.619-5.023 27.76-2.524 20.335 2.524 17.86-20.529 10.483-25.431-3.329-2.211-15.142-9.123-26.984 4.466z"/></svg>''',
    "B": r'''<svg viewBox="-10 -10 336.966 466.595"><path style="fill:#454242" stroke="#fff" stroke-width="10" d="m40.579 192.196.16-.73c6.113-52.851 73.037-113.091 86.404-124.633l.433-3.513a38.117 38.117 0 0 1-9.534-25.205C118.041 17.107 135.126 0 156.134 0s38.115 17.108 38.115 38.116c0 15.67-9.831 29.972-24.475 35.561l-1.232.912c-30.976 49.315-32.641 155.678-32.709 160.171l-.046 3.536 52.006-.274-.045-3.376c-.958-83.416 21.053-129.469 28.33-141.946 77.941 85.446 70.756 128.42 70.665 128.808l-.068.388v.411c1.711 65.715-29.219 110.263-44.388 128.055a1005.352 1005.352 0 0 0-87.704-3.558c-25.775.114-51.801 1.3-77.554 3.421-64.187-73.746-36.747-157.208-36.45-158.029zM316.966 443.607a2.999 2.999 0 0 1-2.988 2.988H2.988A2.999 2.999 0 0 1 0 443.607v-19.571c0-36.427 29.63-66.058 66.057-66.058l.319-.023a990.753 990.753 0 0 1 88.229-4.311c31.934-.114 64.21 1.323 95.984 4.311l.319.023c36.427 0 66.057 29.63 66.057 66.058v19.571z"/></svg>''',
    "R": r'''<svg viewBox="-10 -10 345.497 428.594"><path style="fill:#454242" stroke="#fff" stroke-width="10" d="m29.128 87.43-5.292-52.189a22.465 22.465 0 0 1 5.702-17.336 22.4 22.4 0 0 1 16.651-7.413h28.626c4.881 0 9.17 3.262 10.47 7.961l11.223 40.465 22.171-.798 2.053-51.414A6.97 6.97 0 0 1 127.712 0h65.624a6.944 6.944 0 0 1 6.934 6.295l5.201 52.029 26.414-.753 6.09-37.933c.844-5.315 5.361-9.147 10.721-9.147h25.57a22.485 22.485 0 0 1 16.674 7.413 22.535 22.535 0 0 1 5.68 17.336l-5.269 51.847-.023.342c0 25.73-20.917 46.669-46.646 46.669H75.774c-25.729.001-46.646-20.938-46.646-46.668zM68.315 140.418c2.441.342 4.927.525 7.459.525h168.907c3.308 0 6.524-.319 9.671-.89l22.194 174.564a64.99 64.99 0 0 0-9.945-.753H58.895c-3.421 0-6.775.251-10.036.775l19.456-174.221zM325.497 408.594H0v-28.991c0-32.481 26.414-58.895 58.895-58.895h207.707c32.481 0 58.895 26.414 58.895 58.895v28.991z"/></svg>''',
    "Q": r'''<svg viewBox="-10 -10 456.331 458.452"><path style="fill:#454242" stroke="#fff" stroke-width="10" d="M374.812 393.243c.639 3.034.958 6.227.958 9.603v35.606H58.804v-35.606c0-22.263 15.876-38.07 49.977-49.748 16.309-5.588 65.989-10.31 108.507-10.31 38.275 0 77.827 1.255 107.914 10.356 30.017 9.054 45.755 21.646 49.61 40.099z"/><path style="fill:#454242" stroke="#fff" stroke-width="10" d="m398.238 177.119-2.691 2.144-66.697 167.835c-.57-.182-1.118-.342-1.665-.525-30.953-9.329-71.099-10.63-109.898-10.63-41.446 0-88.069 4.288-107.571 9.717l-64.94-166.604-3.011-2.19C18.339 175.568 0 156.134 0 132.64c0-24.452 19.89-44.343 44.32-44.343 24.452 0 44.342 19.89 44.342 44.343 0 8.713-2.509 17.107-7.276 24.293l.342 4.22 48.197 51.938a3.974 3.974 0 0 0 2.943 1.277 4 4 0 0 0 4.037-3.901l3.079-121.736-2.144-3.262c-16.88-6.774-27.783-22.947-27.783-41.149 0-24.429 19.89-44.32 44.32-44.32 24.452 0 44.342 19.89 44.342 44.32 0 13.161-5.794 25.547-15.899 34.009l-1.072 3.673 32.892 101.185a3.97 3.97 0 0 0 3.809 2.783c1.734 0 3.285-1.095 3.809-2.76l33.508-101.641-1.004-3.673a44.166 44.166 0 0 1-15.419-33.576c0-24.429 19.89-44.32 44.32-44.32 24.452 0 44.342 19.89 44.342 44.32 0 18.978-12.066 35.857-29.995 41.97l-2.327 3.422 6.638 117.334a3.98 3.98 0 0 0 2.715 3.581c.433.16.867.228 1.3.228a4.07 4.07 0 0 0 3.056-1.414l43.362-50.957.388-3.855c-3.581-6.501-5.475-13.914-5.475-21.396 0-24.452 19.89-44.342 44.32-44.342 24.452 0 44.342 19.89 44.342 44.342.002 21.966-16.376 40.852-38.091 43.886z"/></svg>''',
    "K": r'''<svg viewBox="-10 -10 440.106 473.415"><path style="fill:#454242" stroke="#fff" stroke-width="10" d="m400.114 153.397-.159-.183c-46.464-59.192-122.079-32.481-144.159-22.878a9.24 9.24 0 0 1-7.801-.228 9.198 9.198 0 0 1-4.858-6.113l-8.668-36.108h36.633V39.598H233.83V0h-47.558v39.598h-37.271v48.289h36.633l-8.668 36.108a9.2 9.2 0 0 1-4.859 6.113 9.24 9.24 0 0 1-7.801.228c-22.08-9.603-97.695-36.313-144.159 22.878l-.16.183c-2.121 2.349-51.459 58.713 13.253 139.118l63.662 68.749c.981-.251 2.008-.433 3.034-.639.662-.137 1.323-.297 2.007-.41a93.347 93.347 0 0 1 6.387-.958l4.266-.479c27.965-3.079 59.648-6.569 94.889-6.683h2.122c2.783 0 5.566.046 8.326.068.958.023 1.916.023 2.874.023a723.573 723.573 0 0 1 63.617 3.832l.798.069c3.695.388 7.368.821 11.063 1.277.593.068 1.209.137 1.825.205 4.243.525 8.485 1.095 12.728 1.711 2.235.319 4.402.707 6.546 1.163.685.137 1.346.297 2.03.456 1.026.251 2.098.456 3.102.73l64.37-69.114c64.687-80.406 15.349-136.769 13.228-139.118zM169.802 284.804c-.046 5.657-4.676 10.264-10.333 10.264h-4.721c-12.112 0-23.563-4.722-32.231-13.321-17.267-17.085-37.499-42.769-35.697-67.996.502-6.638 4.585-13.093 11.245-17.723 9.535-6.592 30.702-14.37 65.373 7.596 4.151 2.646 6.707 7.345 6.684 12.272l-.32 68.908zm126.96-3.033c-4.334 4.288-9.352 7.618-14.826 9.854a45.578 45.578 0 0 1-17.427 3.444h-4.699c-5.657 0-10.31-4.608-10.333-10.264l-.32-68.909c-.023-4.95 2.509-9.626 6.661-12.272 34.717-21.966 55.839-14.188 65.35-7.596 6.683 4.63 10.789 11.086 11.291 17.723 1.803 25.228-18.407 50.912-35.697 68.02z"/><path style="fill:#454242" stroke="#fff" stroke-width="10" d="M369.48 425.177v21.761a6.474 6.474 0 0 1-6.478 6.478H56.072a6.474 6.474 0 0 1-6.478-6.478v-21.761c0-27.303 18.043-49.064 47.08-56.797 3.9-1.049 8.075-1.825 12.409-2.327l4.265-.456c27.783-3.079 59.283-6.546 94.159-6.66h2.099c33.485 0 67.198 2.395 100.272 7.094 4.471.661 8.759 1.574 12.796 2.737 29.311 8.417 46.806 29.516 46.806 56.409z"/></svg>''',
}


def _parse_svg_paths(svg_text: str) -> tuple[str, list[str]]:
    # Ensure XML namespaces won't break parsing
    if "xmlns" not in svg_text:
        svg_text = svg_text.replace("<svg ", '<svg xmlns="http://www.w3.org/2000/svg" ', 1)

    root = ET.fromstring(svg_text)
    view_box = root.attrib.get("viewBox")
    if not view_box:
        raise ValueError("SVG missing viewBox")

    ns = {"svg": "http://www.w3.org/2000/svg"}
    paths: list[str] = []
    for p in root.findall(".//svg:path", ns):
        style = (p.attrib.get("style") or "").lower()
        fill = (p.attrib.get("fill") or "").lower()
        # Skip highlight / white-detail paths
        if "fill:#fff" in style or "fill:#ffffff" in style or fill in {"#fff", "#ffffff"}:
            continue
        d = p.attrib.get("d")
        if d:
            paths.append(d)

    if not paths:
        raise ValueError("No usable paths found")

    return view_box, paths


def _render_filled_mask(svg_view_box: str, paths: list[str]) -> list[list[bool]]:
    # Render filled silhouette at high resolution, then downsample.
    body = "\n".join([f'<path d="{d}" fill="black" stroke="none"/>' for d in paths])
    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{svg_view_box}">'
        f"{body}"
        "</svg>"
    )
    png = svg2png(bytestring=svg.encode("utf-8"), output_width=RENDER_SIZE, output_height=RENDER_SIZE, background_color="white")
    img = Image.open(__import__("io").BytesIO(png)).convert("L")

    inner = PIECE_SIZE - (PADDING * 2)
    if inner <= 0:
        raise ValueError("Padding too large")

    # Downsample to a slightly smaller sprite, then center it into a 60x60 canvas.
    img_small = img.resize((inner, inner), resample=Image.Resampling.LANCZOS)
    img = Image.new("L", (PIECE_SIZE, PIECE_SIZE), 255)
    img.paste(img_small, (PADDING, PADDING))

    # Black pixels -> True
    px = list(img.getdata())
    mask = [[False for _ in range(PIECE_SIZE)] for _ in range(PIECE_SIZE)]
    for i, v in enumerate(px):
        y = i // PIECE_SIZE
        x = i % PIECE_SIZE
        mask[y][x] = v < 128
    return mask


def _erode(mask: list[list[bool]], iterations: int) -> list[list[bool]]:
    h = len(mask)
    w = len(mask[0])
    cur = [row[:] for row in mask]
    for _ in range(iterations):
        nxt = [[False for _ in range(w)] for _ in range(h)]
        for y in range(h):
            for x in range(w):
                if not cur[y][x]:
                    continue
                ok = True
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        yy = y + dy
                        xx = x + dx
                        if yy < 0 or yy >= h or xx < 0 or xx >= w or not cur[yy][xx]:
                            ok = False
                            break
                    if not ok:
                        break
                nxt[y][x] = ok
        cur = nxt
    return cur


def _outline_from_filled(filled: list[list[bool]], thickness: int) -> list[list[bool]]:
    eroded = _erode(filled, thickness)
    h = len(filled)
    w = len(filled[0])
    out = [[False for _ in range(w)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            out[y][x] = filled[y][x] and (not eroded[y][x])
    return out


def _pack(mask: list[list[bool]]) -> bytes:
    data = bytearray(PIECE_BYTES)
    for y in range(PIECE_SIZE):
        for x in range(PIECE_SIZE):
            if not mask[y][x]:
                continue
            bit_index = y * PIECE_SIZE + x
            byte_index = bit_index // 8
            bit_offset = bit_index % 8
            data[byte_index] |= 1 << bit_offset
    return bytes(data)


def _write_bins(out_dir: str, packed_outline: dict[str, bytes], packed_filled: dict[str, bytes]) -> None:
    os.makedirs(out_dir, exist_ok=True)
    # File names must match src/ChessSprites.cpp SPRITE_FILES.
    order = [
        ("P", "01_pawn_outline.bin", "07_pawn_filled.bin"),
        ("N", "02_knight_outline.bin", "08_knight_filled.bin"),
        ("B", "03_bishop_outline.bin", "09_bishop_filled.bin"),
        ("R", "04_rook_outline.bin", "10_rook_filled.bin"),
        ("Q", "05_queen_outline.bin", "11_queen_filled.bin"),
        ("K", "06_king_outline.bin", "12_king_filled.bin"),
    ]

    for piece, outline_name, filled_name in order:
        with open(os.path.join(out_dir, outline_name), "wb") as f:
            f.write(packed_outline[piece])
        with open(os.path.join(out_dir, filled_name), "wb") as f:
            f.write(packed_filled[piece])


def _hex_array(data: bytes, indent: str = "  ") -> str:
    # 16 bytes per line for readability.
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(lines)


def _write_embedded_cpp(out_h: str, out_cpp: str, packed: list[bytes]) -> None:
    header = textwrap.dedent(
        """\
        #pragma once
        
        #include <cstdint>
        
        namespace EmbeddedChessSprites {
        
        constexpr int PIECE_SIZE = 60;
        constexpr int PIECE_BYTES = (PIECE_SIZE * PIECE_SIZE + 7) / 8;
        
        extern const uint8_t SPRITES[12][PIECE_BYTES];
        
        }  // namespace EmbeddedChessSprites
        """
    )
    with open(out_h, "w", encoding="ascii") as f:
        f.write(header)

    cpp_lines = [
        '#include "EmbeddedChessSprites.h"',
        "",
        "namespace EmbeddedChessSprites {",
        "",
        "const uint8_t SPRITES[12][PIECE_BYTES] = {",
    ]

    for i, blob in enumerate(packed):
        cpp_lines.append("  {")
        cpp_lines.append(_hex_array(blob, indent="    "))
        cpp_lines.append("  },")

    cpp_lines.extend(
        [
            "};",
            "",
            "}  // namespace EmbeddedChessSprites",
            "",
        ]
    )
    with open(out_cpp, "w", encoding="ascii") as f:
        f.write("\n".join(cpp_lines))


def main() -> None:
    packed_outline: dict[str, bytes] = {}
    packed_filled: dict[str, bytes] = {}

    for piece, svg in RAW_SVGS.items():
        view_box, paths = _parse_svg_paths(svg)
        filled = _render_filled_mask(view_box, paths)
        outline = _outline_from_filled(filled, thickness=3)

        filled_bytes = _pack(filled)
        outline_bytes = _pack(outline)

        if len(filled_bytes) != PIECE_BYTES or len(outline_bytes) != PIECE_BYTES:
            raise RuntimeError("Unexpected packed size")

        packed_filled[piece] = filled_bytes
        packed_outline[piece] = outline_bytes

    # Write .bin files for SD override / releases
    _write_bins("assets/sprites", packed_outline, packed_filled)

    # Compose embedded sprite order: 1-6 outline, 7-12 filled
    order = ["P", "N", "B", "R", "Q", "K"]
    packed_all: list[bytes] = [packed_outline[p] for p in order] + [packed_filled[p] for p in order]

    _write_embedded_cpp(
        out_h=os.path.join("src", "EmbeddedChessSprites.h"),
        out_cpp=os.path.join("src", "EmbeddedChessSprites.cpp"),
        packed=packed_all,
    )

    print("Generated sprites:")
    print("- assets/sprites/*.bin")
    print("- src/EmbeddedChessSprites.h")
    print("- src/EmbeddedChessSprites.cpp")


if __name__ == "__main__":
    main()
