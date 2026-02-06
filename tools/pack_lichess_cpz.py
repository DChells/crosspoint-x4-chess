#!/usr/bin/env python3
# pyright: basic
"""Pack chess puzzles into CPZ1 format with 128-byte records.

Supports:
- Lichess CSV input (lichess_db_puzzle.csv)
- Built-in handcrafted starter pack (--starter)
- Theme bitset index generation under assets/index/<packName>/
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import random
import re
import struct
from dataclasses import dataclass


RECORD_SIZE = 128
HEADER_SIZE = 18
MAX_MOVES = 24


@dataclass
class PuzzleEntry:
    fen: str
    moves_uci: list[str]
    rating: int
    themes_raw: str
    opening_raw: str


def require_python_chess():
    try:
        import chess  # type: ignore

        return chess
    except ImportError as exc:
        raise SystemExit(
            "python-chess is required for CPZ packing. Install it with: "
            "python3 -m pip install python-chess"
        ) from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pack puzzles into CPZ1 + theme index bitsets")
    parser.add_argument("--input", help="Path to lichess_db_puzzle.csv")
    parser.add_argument("--output", required=True, help="Output .cpz file path")
    parser.add_argument("--limit", type=int, default=0, help="Limit puzzle count after filtering")
    parser.add_argument("--min-rating", type=int, help="Minimum rating filter")
    parser.add_argument("--max-rating", type=int, help="Maximum rating filter")
    parser.add_argument("--seed", type=int, help="Seed for deterministic ordering/sampling")
    parser.add_argument("--out-dir", default="assets", help="Assets root (default: assets)")
    parser.add_argument(
        "--starter",
        action="store_true",
        help="Generate built-in handcrafted starter pack (no CSV input required)",
    )
    return parser.parse_args()


def sanitize_theme(theme: str) -> str:
    t = theme.strip().lower()
    t = re.sub(r"[^a-z0-9_\-]+", "_", t)
    t = re.sub(r"_+", "_", t).strip("_")
    return t


def parse_themes(raw: str) -> list[str]:
    if not raw:
        return []
    chunks = re.split(r"[\s,;|]+", raw.strip())
    themes: list[str] = []
    seen: set[str] = set()
    for chunk in chunks:
        if not chunk:
            continue
        theme = sanitize_theme(chunk)
        if not theme or theme in seen:
            continue
        seen.add(theme)
        themes.append(theme)
    return themes


def sanitize_field(text: str, size: int) -> bytes:
    clean = re.sub(r"\s+", " ", (text or "").strip())
    payload = clean.encode("ascii", errors="ignore")[:size]
    return payload + b"\x00" * (size - len(payload))


def promotion_code(uci: str) -> int:
    if len(uci) < 5:
        return 0
    promo = uci[4].lower()
    return {"n": 1, "b": 2, "r": 3, "q": 4}.get(promo, 0)


def square_from_uci(chess_mod, sq: str) -> int:
    return chess_mod.parse_square(sq)


def move_to_packed(chess_mod, uci: str) -> int:
    if len(uci) < 4:
        raise ValueError(f"Invalid UCI move: {uci}")
    from_sq = square_from_uci(chess_mod, uci[:2])
    to_sq = square_from_uci(chess_mod, uci[2:4])
    promo = promotion_code(uci)
    return (from_sq & 0x3F) | ((to_sq & 0x3F) << 6) | ((promo & 0x0F) << 12)


def board_piece_nibble(chess_mod, board, square: int) -> int:
    piece = board.piece_at(square)
    if piece is None:
        return 0
    base = piece.piece_type
    return base if piece.color == chess_mod.WHITE else (base + 6)


def pack_board_nibbles(chess_mod, board) -> bytes:
    out = bytearray(32)
    for i in range(32):
        sq1 = i * 2
        sq2 = i * 2 + 1
        n1 = board_piece_nibble(chess_mod, board, sq1) & 0x0F
        n2 = board_piece_nibble(chess_mod, board, sq2) & 0x0F
        out[i] = n1 | (n2 << 4)
    return bytes(out)


def encode_ep_file(chess_mod, board) -> int:
    ep_sq = board.ep_square
    if ep_sq is None:
        return 7

    ep_file = chess_mod.square_file(ep_sq)
    ep_rank = chess_mod.square_rank(ep_sq)
    expected_rank = 5 if board.turn == chess_mod.WHITE else 2

    if ep_file > 6:
        return 7
    if ep_rank != expected_rank:
        return 7
    if not board.has_legal_en_passant():
        return 7
    return ep_file


def encode_flags(chess_mod, board) -> int:
    flags = 0
    if board.turn == chess_mod.WHITE:
        flags |= 1
    if board.has_kingside_castling_rights(chess_mod.WHITE):
        flags |= 1 << 1
    if board.has_queenside_castling_rights(chess_mod.WHITE):
        flags |= 1 << 2
    if board.has_kingside_castling_rights(chess_mod.BLACK):
        flags |= 1 << 3
    if board.has_queenside_castling_rights(chess_mod.BLACK):
        flags |= 1 << 4
    flags |= (encode_ep_file(chess_mod, board) & 0x07) << 5
    return flags


def validate_moves(chess_mod, fen: str, moves_uci: list[str]) -> None:
    board = chess_mod.Board(fen)
    for uci in moves_uci:
        move = chess_mod.Move.from_uci(uci)
        if move not in board.legal_moves:
            raise ValueError(f"Illegal move '{uci}' for FEN '{fen}'")
        board.push(move)


def opening_from_tag(raw: str) -> str:
    text = (raw or "").strip()
    if not text:
        return ""
    first = text.split()[0]
    return first.replace("_", " ")


def record_from_entry(chess_mod, entry: PuzzleEntry) -> tuple[bytes, list[str]]:
    if len(entry.moves_uci) > MAX_MOVES:
        raise ValueError("too_many_moves")

    validate_moves(chess_mod, entry.fen, entry.moves_uci)
    board = chess_mod.Board(entry.fen)

    record = bytearray(RECORD_SIZE)
    record[0:2] = struct.pack("<H", max(0, min(65535, int(entry.rating))))
    record[2] = encode_flags(chess_mod, board)
    record[3] = len(entry.moves_uci)
    record[4:36] = pack_board_nibbles(chess_mod, board)

    for i, uci in enumerate(entry.moves_uci):
        packed = move_to_packed(chess_mod, uci)
        offset = 36 + i * 2
        record[offset : offset + 2] = struct.pack("<H", packed)

    themes = parse_themes(entry.themes_raw)
    themes_field = ",".join(themes)
    opening_field = opening_from_tag(entry.opening_raw)
    record[84:116] = sanitize_field(themes_field, 32)
    record[116:128] = sanitize_field(opening_field, 12)

    return bytes(record), themes


def load_lichess_csv(path: pathlib.Path) -> list[PuzzleEntry]:
    entries: list[PuzzleEntry] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        required = {"FEN", "Moves", "Rating", "Themes"}
        if not required.issubset(set(reader.fieldnames or [])):
            missing = sorted(required - set(reader.fieldnames or []))
            raise SystemExit(f"CSV missing required columns: {', '.join(missing)}")

        for row in reader:
            fen = (row.get("FEN") or "").strip()
            moves_raw = (row.get("Moves") or "").strip()
            if not fen or not moves_raw:
                continue
            moves_uci = [m for m in moves_raw.split() if m]
            if len(moves_uci) > MAX_MOVES:
                continue

            try:
                rating = int(float((row.get("Rating") or "0").strip() or 0))
            except ValueError:
                rating = 0

            opening_raw = (row.get("OpeningTags") or "").strip()
            entries.append(
                PuzzleEntry(
                    fen=fen,
                    moves_uci=moves_uci,
                    rating=rating,
                    themes_raw=(row.get("Themes") or "").strip(),
                    opening_raw=opening_raw,
                )
            )
    return entries


def starter_entries() -> list[PuzzleEntry]:
    return [
        PuzzleEntry(
            fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            moves_uci=["e2e4", "e7e5", "g1f3"],
            rating=700,
            themes_raw="opening development",
            opening_raw="King_Pawn_Game",
        ),
        PuzzleEntry(
            fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            moves_uci=["d2d4", "d7d5", "c2c4"],
            rating=750,
            themes_raw="opening gambit",
            opening_raw="Queens_Gambit",
        ),
        PuzzleEntry(
            fen="r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/8/PPPP1PPP/RNBQK1NR w KQkq - 2 3",
            moves_uci=["d1h5", "g8f6", "h5f7"],
            rating=900,
            themes_raw="mate attack",
            opening_raw="Italian_Game",
        ),
        PuzzleEntry(
            fen="6k1/5ppp/8/8/8/6Q1/6PP/6K1 w - - 0 1",
            moves_uci=["g3b8"],
            rating=820,
            themes_raw="skewer queenmove",
            opening_raw="Miniature",
        ),
        PuzzleEntry(
            fen="7k/P7/8/8/8/8/8/K7 w - - 0 1",
            moves_uci=["a7a8q"],
            rating=880,
            themes_raw="promotion",
            opening_raw="Pawn_Ending",
        ),
        PuzzleEntry(
            fen="6k1/7P/8/8/8/8/8/K7 w - - 0 1",
            moves_uci=["h7h8n"],
            rating=980,
            themes_raw="underpromotion knight",
            opening_raw="Pawn_Ending",
        ),
        PuzzleEntry(
            fen="4k3/8/8/3Pp3/8/8/8/4K3 w - e6 0 1",
            moves_uci=["d5e6"],
            rating=860,
            themes_raw="en_passant",
            opening_raw="Special_Rule",
        ),
        PuzzleEntry(
            fen="r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            moves_uci=["e1g1", "e8c8"],
            rating=830,
            themes_raw="castling",
            opening_raw="Castling_Example",
        ),
        PuzzleEntry(
            fen="6k1/5ppp/8/8/8/8/5PPP/5RK1 w - - 0 1",
            moves_uci=["f1e1"],
            rating=780,
            themes_raw="rook_move prophylaxis",
            opening_raw="Rook_Ending",
        ),
        PuzzleEntry(
            fen="5rk1/5ppp/8/8/8/8/5PPP/5RK1 b - - 0 1",
            moves_uci=["f8e8"],
            rating=790,
            themes_raw="rook_move defense",
            opening_raw="Rook_Ending",
        ),
    ]


def apply_filters(entries: list[PuzzleEntry], args: argparse.Namespace) -> list[PuzzleEntry]:
    filtered = []
    for entry in entries:
        if args.min_rating is not None and entry.rating < args.min_rating:
            continue
        if args.max_rating is not None and entry.rating > args.max_rating:
            continue
        filtered.append(entry)

    rng = random.Random(args.seed)
    if args.seed is not None:
        rng.shuffle(filtered)

    if args.limit and args.limit > 0:
        filtered = filtered[: args.limit]
    return filtered


def write_theme_indexes(
    index_root: pathlib.Path,
    pack_name: str,
    puzzle_count: int,
    themes_per_puzzle: list[list[str]],
) -> None:
    pack_index_dir = index_root / "index" / pack_name
    pack_index_dir.mkdir(parents=True, exist_ok=True)

    all_themes: set[str] = set()
    for themes in themes_per_puzzle:
        all_themes.update(themes)

    bitset_size = (puzzle_count + 7) // 8
    for theme in sorted(all_themes):
        bitset = bytearray(bitset_size)
        for idx, themes in enumerate(themes_per_puzzle):
            if theme in themes:
                bitset[idx // 8] |= 1 << (idx % 8)
        (pack_index_dir / f"theme_{theme}.bit").write_bytes(bytes(bitset))


def build_cpz(records: list[bytes], ratings: list[int]) -> bytes:
    if any(len(r) != RECORD_SIZE for r in records):
        raise ValueError("All records must be 128 bytes")

    count = len(records)
    rating_min = min(ratings) if ratings else 0
    rating_max = max(ratings) if ratings else 0

    header = bytearray(HEADER_SIZE)
    header[0:4] = b"CPZ1"
    header[4:6] = struct.pack("<H", RECORD_SIZE)
    header[6:10] = struct.pack("<I", count)
    header[10:12] = struct.pack("<H", rating_min)
    header[12:14] = struct.pack("<H", rating_max)
    header[14:18] = b"\x00\x00\x00\x00"

    return bytes(header) + b"".join(records)


def inspect_blob(blob: bytes) -> None:
    if len(blob) < HEADER_SIZE:
        raise SystemExit("CPZ output too small")
    if blob[0:4] != b"CPZ1":
        raise SystemExit("CPZ magic mismatch")

    record_size = struct.unpack_from("<H", blob, 4)[0]
    puzzle_count = struct.unpack_from("<I", blob, 6)[0]
    if record_size != RECORD_SIZE:
        raise SystemExit(f"Unexpected record size: {record_size}")
    if len(blob) != HEADER_SIZE + puzzle_count * record_size:
        raise SystemExit("CPZ size does not match header count/recordSize")

    if puzzle_count > 0:
        rec = blob[HEADER_SIZE : HEADER_SIZE + record_size]
        move_count = rec[3]
        if move_count > MAX_MOVES:
            raise SystemExit("First record moveCount exceeds MAX_MOVES")


def main() -> None:
    args = parse_args()

    if not args.starter and not args.input:
        raise SystemExit("Provide --input CSV or use --starter")

    chess = require_python_chess()

    if args.starter:
        entries = starter_entries()
    else:
        entries = load_lichess_csv(pathlib.Path(args.input))

    entries = apply_filters(entries, args)

    records: list[bytes] = []
    themes_per_puzzle: list[list[str]] = []
    ratings: list[int] = []
    skipped = 0

    for entry in entries:
        try:
            record, themes = record_from_entry(chess, entry)
        except ValueError as exc:
            if str(exc) == "too_many_moves":
                skipped += 1
                continue
            skipped += 1
            continue
        records.append(record)
        themes_per_puzzle.append(themes)
        ratings.append(entry.rating)

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    cpz_blob = build_cpz(records, ratings)
    inspect_blob(cpz_blob)
    out_path.write_bytes(cpz_blob)

    pack_name = out_path.stem
    write_theme_indexes(pathlib.Path(args.out_dir), pack_name, len(records), themes_per_puzzle)

    print(
        f"Wrote {out_path} ({len(records)} puzzles, {len(cpz_blob)} bytes). "
        f"Skipped: {skipped}. Index: {pathlib.Path(args.out_dir) / 'index' / pack_name}"
    )


if __name__ == "__main__":
    main()
