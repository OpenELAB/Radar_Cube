"""
Generate voice WAV files from 雷达项目音效清单设计.md.

This script only generates entries that have both a WAV filename and a spoken
voice prompt. Distance beep assets are intentionally skipped.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from mimo_tts_to_wav import (
    BUILTIN_MODE,
    BUILTIN_MODEL,
    DEFAULT_BASE_URL,
    DEFAULT_BUILTIN_STYLE,
    DEFAULT_SAMPLE_RATE,
    DEFAULT_VOICE,
    generate_wav,
)


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_SOURCE = SCRIPT_DIR / "雷达项目音效清单设计.md"
DEFAULT_OUT_DIR = SCRIPT_DIR / "data"
DEFAULT_EXPECTED_COUNT = 34


@dataclass(frozen=True)
class VoiceEntry:
    filename: str
    text: str


def split_markdown_row(line: str) -> list[str]:
    if not line.startswith("|"):
        return []
    return [part.strip() for part in line.strip().strip("|").split("|")]


def extract_wav_filename(value: str) -> str | None:
    match = re.search(r"`([^`]+\.wav)`", value)
    if match:
        return match.group(1).strip()

    value = value.strip()
    if re.fullmatch(r"[a-z0-9_]+\.wav", value):
        return value

    return None


def is_voice_text(value: str) -> bool:
    value = value.strip()
    if not value or value == "播报语音":
        return False
    return not any(marker in value for marker in ("无语音", "仅蜂鸣"))


def parse_voice_entries(source_path: Path) -> list[VoiceEntry]:
    entries: list[VoiceEntry] = []

    for line in source_path.read_text(encoding="utf-8").splitlines():
        columns = split_markdown_row(line)
        if len(columns) < 5:
            continue

        filename = extract_wav_filename(columns[3])
        if not filename:
            continue

        if filename.startswith("dist_beep_"):
            continue

        text = columns[4].strip()
        if not is_voice_text(text):
            continue

        entries.append(VoiceEntry(filename=filename, text=text))

    return entries


def print_entries(entries: list[VoiceEntry]) -> None:
    for index, entry in enumerate(entries, start=1):
        print(f"{index:02d}. {entry.filename} <- {entry.text}")
    print(f"\nCOUNT={len(entries)}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate MiMo TTS voice WAV files from the radar sound list."
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List parsed voice entries without calling the MiMo API.",
    )
    parser.add_argument(
        "--source",
        default=str(DEFAULT_SOURCE),
        help="Path to 雷达项目音效清单设计.md.",
    )
    parser.add_argument(
        "--out-dir",
        default=str(DEFAULT_OUT_DIR),
        help="Output directory for generated WAV files.",
    )
    parser.add_argument(
        "--voice",
        default=DEFAULT_VOICE,
        help="MiMo builtin voice ID. Default: 茉莉.",
    )
    parser.add_argument(
        "--skip-existing",
        action="store_true",
        help="Skip output files that already exist. Default behavior overwrites.",
    )
    parser.add_argument(
        "--expected-count",
        type=int,
        default=DEFAULT_EXPECTED_COUNT,
        help="Expected parsed entry count. Use 0 to disable the warning.",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help="MiMo OpenAI-compatible API base URL.",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help="Output sample rate. Default: 16000.",
    )
    return parser.parse_args(argv)


def ensure_can_generate() -> None:
    if not os.environ.get("MIMO_API_KEY"):
        raise RuntimeError(
            "MIMO_API_KEY is not set. In PowerShell, run: "
            '$env:MIMO_API_KEY="your_api_key"'
        )


def generate_entries(args: argparse.Namespace, entries: list[VoiceEntry]) -> int:
    out_dir = Path(args.out_dir).expanduser()
    out_dir.mkdir(parents=True, exist_ok=True)

    successes: list[VoiceEntry] = []
    skipped: list[VoiceEntry] = []
    failures: list[tuple[VoiceEntry, str]] = []

    for index, entry in enumerate(entries, start=1):
        output_path = out_dir / entry.filename

        if args.skip_existing and output_path.exists():
            skipped.append(entry)
            print(f"[{index:02d}/{len(entries):02d}] SKIP {entry.filename}")
            continue

        print(f"[{index:02d}/{len(entries):02d}] GEN  {entry.filename} <- {entry.text}")
        try:
            generate_wav(
                text=entry.text,
                output_file=str(output_path),
                mode=BUILTIN_MODE,
                voice=args.voice,
                style=DEFAULT_BUILTIN_STYLE,
                style_tags=None,
                model=BUILTIN_MODEL,
                optimize_text_preview=False,
                sample_rate=args.sample_rate,
                keep_temp=False,
                base_url=args.base_url,
            )
            successes.append(entry)
        except Exception as exc:
            failures.append((entry, str(exc)))
            print(f"[ERROR] {entry.filename}: {exc}")

    print("\nSummary")
    print(f"  success: {len(successes)}")
    print(f"  skipped: {len(skipped)}")
    print(f"  failed : {len(failures)}")

    if failures:
        print("\nFailures")
        for entry, error in failures:
            print(f"  - {entry.filename} <- {entry.text}: {error}")
        return 1

    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    source_path = Path(args.source).expanduser()

    if not source_path.exists():
        print(f"[ERROR] Source file not found: {source_path}", file=sys.stderr)
        return 1

    entries = parse_voice_entries(source_path)
    if args.expected_count and len(entries) != args.expected_count:
        print(
            f"[WARN] Parsed {len(entries)} entries, expected {args.expected_count}.",
            file=sys.stderr,
        )

    if args.list:
        print_entries(entries)
        return 0

    try:
        ensure_can_generate()
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1

    return generate_entries(args, entries)


if __name__ == "__main__":
    raise SystemExit(main())
