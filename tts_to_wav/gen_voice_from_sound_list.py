"""Generate voice WAV files from 音频与音效清单.md."""

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
DEFAULT_SOURCE = SCRIPT_DIR / "音频与音效清单.md"
DEFAULT_OUT_DIR = SCRIPT_DIR / "data"
DEFAULT_EXPECTED_COUNT = 22

COLUMN_ALIASES = {
    "文件名": ("文件名",),
    "类型": ("类型",),
    "播报内容": ("播报内容", "播报内容/声音说明"),
}
ALLOWED_AUDIO_TYPES = {"语音", "短音效", "距离蜂鸣"}
VOICE_AUDIO_TYPE = "语音"


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
        value = match.group(1).strip()
    else:
        value = value.strip()

    if re.fullmatch(r"[a-z0-9_]+\.wav", value):
        return value

    return None


def parse_voice_entries(source_path: Path) -> list[VoiceEntry]:
    entries: list[VoiceEntry] = []
    column_indexes: dict[str, int] | None = None
    seen_filenames: set[str] = set()

    for line_number, line in enumerate(
        source_path.read_text(encoding="utf-8").splitlines(),
        start=1,
    ):
        columns = split_markdown_row(line)
        if not columns:
            continue

        matched_columns = {
            logical_name: next(
                (alias for alias in aliases if alias in columns),
                None,
            )
            for logical_name, aliases in COLUMN_ALIASES.items()
        }
        if all(matched_columns.values()):
            column_indexes = {
                logical_name: columns.index(alias)
                for logical_name, alias in matched_columns.items()
                if alias is not None
            }
            continue

        if column_indexes is None:
            continue

        if all(re.fullmatch(r":?-{3,}:?", column) for column in columns):
            continue

        last_required_index = max(column_indexes.values())
        if len(columns) <= last_required_index:
            raise ValueError(f"第 {line_number} 行缺少必需列")

        raw_filename = columns[column_indexes["文件名"]]
        filename = extract_wav_filename(raw_filename)
        if not filename:
            raise ValueError(
                f"第 {line_number} 行的 WAV 文件名无效: {raw_filename or '<空>'}"
            )

        if filename in seen_filenames:
            raise ValueError(f"第 {line_number} 行存在重复的 WAV 文件名: {filename}")
        seen_filenames.add(filename)

        audio_type = columns[column_indexes["类型"]].strip()
        if audio_type not in ALLOWED_AUDIO_TYPES:
            raise ValueError(f"第 {line_number} 行的音频类型无效: {audio_type or '<空>'}")

        if audio_type != VOICE_AUDIO_TYPE:
            continue

        text = columns[column_indexes["播报内容"]].strip()
        if not text:
            raise ValueError(f"第 {line_number} 行的播报内容不能为空: {filename}")

        entries.append(VoiceEntry(filename=filename, text=text))

    if column_indexes is None:
        required = "、".join(COLUMN_ALIASES)
        raise ValueError(f"未找到包含以下列的音频资源表: {required}")

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
        help="Path to 音频与音效清单.md.",
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
