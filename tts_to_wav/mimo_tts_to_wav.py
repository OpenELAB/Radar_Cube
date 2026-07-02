"""
Generate project-ready WAV files with Xiaomi MiMo-V2.5-TTS.

The MiMo API returns base64 encoded audio. This script saves that source audio
to a temporary WAV file, then normalizes it with ffmpeg to the radar project's
required format: 16 kHz, 16-bit, mono, PCM WAV.
"""

from __future__ import annotations

import argparse
import base64
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_BASE_URL = "https://api.xiaomimimo.com/v1"
VOICE_DESIGN_MODE = "voice-design"
BUILTIN_MODE = "builtin"
DEFAULT_MODE = VOICE_DESIGN_MODE
VOICE_DESIGN_MODEL = "mimo-v2.5-tts-voicedesign"
BUILTIN_MODEL = "mimo-v2.5-tts"
DEFAULT_MODEL = VOICE_DESIGN_MODEL
DEFAULT_VOICE = "茉莉"
DEFAULT_VOICE_DESIGN = (
    "年轻女性车载安全提示音，普通话标准，声音清晰明亮，语气冷静，短促直接。"
)
DEFAULT_BUILTIN_STYLE = (
    "车载安全提示音。标准普通话，吐字清晰，语速略快，语气冷静克制；"
    "不要夸张表演，不要拖长尾音，适合 1-2 秒短提示。"
)
DEFAULT_STYLE = DEFAULT_BUILTIN_STYLE
DEFAULT_STYLE_TAGS = None
DEFAULT_SAMPLE_RATE = 16000
DEFAULT_CHANNELS = 1
DEFAULT_BITS_PER_SAMPLE = 16

FFMPEG_PATHS = [
    r"C:\Users\zhangyu\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe",
    r"C:\Program Files\FFmpeg\bin\ffmpeg.exe",
    r"C:\ffmpeg\bin\ffmpeg.exe",
]

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIR = SCRIPT_DIR / "data"


def setup_ffmpeg() -> str | None:
    """Return an ffmpeg executable path if available."""
    ffmpeg_from_path = shutil.which("ffmpeg")
    if ffmpeg_from_path:
        print("[OK] Found ffmpeg in PATH")
        return ffmpeg_from_path

    for path in FFMPEG_PATHS:
        if Path(path).exists():
            print(f"[OK] Found ffmpeg: {path}")
            return path

    print("[WARN] ffmpeg was not found. Install it before generating WAV files:")
    print("  winget install Gyan.FFmpeg")
    return None


def resolve_output_path(output_file: str) -> Path:
    """Resolve output path while keeping simple filenames under this script dir."""
    output_path = Path(output_file).expanduser()
    if output_path.is_absolute():
        return output_path.with_suffix(".wav")

    if output_path.parent == Path("."):
        return (SCRIPT_DIR / output_path).with_suffix(".wav")

    return (Path.cwd() / output_path).with_suffix(".wav")


def ensure_wav_suffix(path: Path) -> Path:
    return path if path.suffix.lower() == ".wav" else path.with_suffix(".wav")


def sanitize_output_stem(text: str, index: int) -> str:
    text = re.sub(r"\s+", "_", text.strip())
    text = "".join(ch for ch in text if ch.isalnum() or ch in ("_", "-"))
    text = text.strip("._-")
    if not text:
        text = f"tts_{index:03d}"
    return text[:48]


def unique_output_path(output_dir: Path, text: str, index: int) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = sanitize_output_stem(text, index)
    candidate = output_dir / f"{stem}.wav"
    suffix = 2
    while candidate.exists():
        candidate = output_dir / f"{stem}_{suffix}.wav"
        suffix += 1
    return candidate


def parse_interactive_text_line(line: str) -> tuple[str, str | None]:
    if "=>" not in line:
        return line.strip(), None

    text, output_file = line.split("=>", 1)
    text = text.strip()
    output_file = output_file.strip().strip('"')
    return text, output_file or None


def require_api_key() -> str:
    api_key = os.environ.get("MIMO_API_KEY")
    if not api_key:
        raise RuntimeError(
            "MIMO_API_KEY is not set. In PowerShell, run: "
            '$env:MIMO_API_KEY="your_api_key"'
        )
    return api_key


def decode_audio_data(completion: object) -> bytes:
    """Extract and decode choices[0].message.audio.data from the OpenAI client object."""
    try:
        message = completion.choices[0].message  # type: ignore[attr-defined]
        audio = message.audio
    except (AttributeError, IndexError, TypeError) as exc:
        raise RuntimeError("MiMo response did not contain choices[0].message.audio.data") from exc

    if isinstance(audio, dict):
        audio_data = audio.get("data")
    else:
        audio_data = getattr(audio, "data", None)

    if not isinstance(audio_data, str) or not audio_data:
        raise RuntimeError("MiMo response audio.data is empty or not a string")

    try:
        return base64.b64decode(audio_data)
    except Exception as exc:
        raise RuntimeError("MiMo response audio.data was not valid base64") from exc


def build_assistant_content(text: str, style_tags: str | None) -> str:
    """Add MiMo audio style tags to the assistant synthesis text."""
    clean_text = text.strip()
    clean_tags = (style_tags or "").strip()
    if not clean_tags:
        return clean_text

    if clean_tags[0] in "([（[":
        return f"{clean_tags}{clean_text}"

    return f"({clean_tags}){clean_text}"


def build_request_kwargs(
    *,
    text: str,
    mode: str = DEFAULT_MODE,
    voice: str = DEFAULT_VOICE,
    style: str = DEFAULT_BUILTIN_STYLE,
    voice_design: str = DEFAULT_VOICE_DESIGN,
    style_tags: str | None = DEFAULT_STYLE_TAGS,
    model: str | None = None,
    optimize_text_preview: bool = False,
) -> dict[str, object]:
    """Build MiMo chat completion kwargs for supported TTS modes."""
    assistant_content = build_assistant_content(text, style_tags)

    if mode == VOICE_DESIGN_MODE:
        audio: dict[str, object] = {"format": "wav"}
        if optimize_text_preview:
            audio["optimize_text_preview"] = True

        return {
            "model": model or VOICE_DESIGN_MODEL,
            "messages": [
                {"role": "user", "content": voice_design},
                {"role": "assistant", "content": assistant_content},
            ],
            "audio": audio,
        }

    if mode == BUILTIN_MODE:
        return {
            "model": model or BUILTIN_MODEL,
            "messages": [
                {"role": "user", "content": style},
                {"role": "assistant", "content": assistant_content},
            ],
            "audio": {"format": "wav", "voice": voice},
        }

    raise ValueError(f"Unsupported mode: {mode}")


def synthesize_source_wav(
    *,
    text: str,
    source_wav: Path,
    mode: str,
    voice: str,
    style: str,
    voice_design: str,
    style_tags: str | None,
    model: str | None,
    optimize_text_preview: bool,
    base_url: str,
) -> None:
    api_key = require_api_key()

    try:
        from openai import OpenAI
    except ImportError as exc:
        raise RuntimeError(
            "The openai package is required. Install it with: pip install openai"
        ) from exc

    client = OpenAI(api_key=api_key, base_url=base_url)
    request_kwargs = build_request_kwargs(
        text=text,
        mode=mode,
        voice=voice,
        style=style,
        voice_design=voice_design,
        style_tags=style_tags,
        model=model,
        optimize_text_preview=optimize_text_preview,
    )
    print(f"[TTS] Requesting MiMo speech synthesis: model={request_kwargs['model']}, mode={mode}")

    completion = client.chat.completions.create(**request_kwargs)

    audio_bytes = decode_audio_data(completion)
    source_wav.parent.mkdir(parents=True, exist_ok=True)
    source_wav.write_bytes(audio_bytes)
    print(f"[OK] Saved MiMo source WAV: {source_wav}")


def convert_to_project_wav(
    *,
    ffmpeg_path: str,
    source_wav: Path,
    output_path: Path,
    sample_rate: int,
    channels: int,
    bits_per_sample: int,
) -> None:
    if bits_per_sample != 16:
        raise ValueError("Only 16-bit PCM WAV output is supported")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg_path,
        "-y",
        "-i",
        str(source_wav),
        "-acodec",
        "pcm_s16le",
        "-ar",
        str(sample_rate),
        "-ac",
        str(channels),
        str(output_path),
    ]

    print(
        "[CONV] Normalizing WAV: "
        f"{sample_rate}Hz, {bits_per_sample}bit, {channels} channel(s)"
    )
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg conversion failed:\n{result.stderr}")

    print(f"[OK] Project WAV generated: {output_path}")


def generate_wav(
    text: str,
    output_file: str = "output.wav",
    mode: str = DEFAULT_MODE,
    voice: str = DEFAULT_VOICE,
    style: str = DEFAULT_BUILTIN_STYLE,
    voice_design: str = DEFAULT_VOICE_DESIGN,
    style_tags: str | None = DEFAULT_STYLE_TAGS,
    model: str | None = None,
    optimize_text_preview: bool = False,
    sample_rate: int = DEFAULT_SAMPLE_RATE,
    channels: int = DEFAULT_CHANNELS,
    bits_per_sample: int = DEFAULT_BITS_PER_SAMPLE,
    keep_temp: bool = False,
    base_url: str = DEFAULT_BASE_URL,
) -> Path:
    """Generate a project-ready WAV file and return its path."""
    if not text.strip():
        raise ValueError("text must not be empty")

    require_api_key()
    ffmpeg_path = setup_ffmpeg()
    if not ffmpeg_path:
        raise RuntimeError("ffmpeg is required")

    output_path = resolve_output_path(output_file)
    temp_source_wav = output_path.with_suffix(".mimo_source.wav")

    try:
        synthesize_source_wav(
            text=text,
            source_wav=temp_source_wav,
            mode=mode,
            voice=voice,
            style=style,
            voice_design=voice_design,
            style_tags=style_tags,
            model=model,
            optimize_text_preview=optimize_text_preview,
            base_url=base_url,
        )
        convert_to_project_wav(
            ffmpeg_path=ffmpeg_path,
            source_wav=temp_source_wav,
            output_path=output_path,
            sample_rate=sample_rate,
            channels=channels,
            bits_per_sample=bits_per_sample,
        )
    finally:
        if not keep_temp:
            temp_source_wav.unlink(missing_ok=True)
        elif temp_source_wav.exists():
            print(f"[KEEP] Source WAV kept: {temp_source_wav}")

    return output_path


def prompt_with_default(label: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default not in (None, "") else ""
    value = input(f"{label}{suffix}: ").strip()
    return value if value else (default or "")


def prompt_choice(label: str, choices: list[str], default: str) -> str:
    choice_text = "/".join(choices)
    while True:
        value = prompt_with_default(f"{label} ({choice_text})", default)
        if value in choices:
            return value
        print(f"[WARN] 请输入: {choice_text}")


def prompt_yes_no(label: str, default: bool = False) -> bool:
    default_text = "y" if default else "n"
    while True:
        value = prompt_with_default(f"{label} (y/n)", default_text).lower()
        if value in ("y", "yes"):
            return True
        if value in ("n", "no"):
            return False
        print("[WARN] 请输入 y 或 n")


def prompt_int(label: str, default: int) -> int:
    while True:
        value = prompt_with_default(label, str(default))
        try:
            return int(value)
        except ValueError:
            print("[WARN] 请输入整数")


def collect_interactive_config(args: argparse.Namespace | None = None) -> dict[str, object]:
    args = args or argparse.Namespace()
    default_mode = getattr(args, "mode", DEFAULT_MODE)
    default_voice = getattr(args, "voice", DEFAULT_VOICE)
    default_voice_design = getattr(args, "voice_design", None) or DEFAULT_VOICE_DESIGN
    default_style = getattr(args, "style", None) or DEFAULT_BUILTIN_STYLE
    default_style_tags = getattr(args, "style_tags", DEFAULT_STYLE_TAGS)
    default_sample_rate = getattr(args, "sample_rate", DEFAULT_SAMPLE_RATE)
    default_keep_temp = getattr(args, "keep_temp", False)
    default_base_url = getattr(args, "base_url", DEFAULT_BASE_URL)

    print("=" * 72)
    print("  MiMo TTS 批量生成模式")
    print("  先配置一次参数，然后重复输入文本生成 WAV。输入 q 退出。")
    print("=" * 72)

    mode = prompt_choice("模式", [VOICE_DESIGN_MODE, BUILTIN_MODE], default_mode)
    voice = default_voice
    voice_design = default_voice_design
    style = default_style
    optimize_text_preview = False

    if mode == VOICE_DESIGN_MODE:
        voice_design = prompt_with_default("文本设计音色描述", default_voice_design)
        optimize_text_preview = prompt_yes_no("是否启用文本优化预览（可能扩写文本）", False)
    else:
        voice = prompt_with_default("内置音色 ID", default_voice)
        style = prompt_with_default("内置音色风格提示", default_style)

    style_tags = prompt_with_default("音频标签（空=不加）", default_style_tags or "") or None
    output_dir_value = prompt_with_default("输出目录", str(DEFAULT_OUTPUT_DIR))
    sample_rate = prompt_int("输出采样率", default_sample_rate)
    keep_temp = prompt_yes_no("是否保留 MiMo 原始 WAV", default_keep_temp)

    return {
        "mode": mode,
        "voice": voice,
        "voice_design": voice_design,
        "style": style,
        "style_tags": style_tags,
        "optimize_text_preview": optimize_text_preview,
        "output_dir": Path(output_dir_value).expanduser(),
        "sample_rate": sample_rate,
        "keep_temp": keep_temp,
        "base_url": default_base_url,
        "model": getattr(args, "model", None),
    }


def print_interactive_config(config: dict[str, object]) -> None:
    print("\n当前配置:")
    print(f"  模式: {config['mode']}")
    if config["mode"] == VOICE_DESIGN_MODE:
        print(f"  文本设计音色: {config['voice_design']}")
        print(f"  文本优化预览: {'开' if config['optimize_text_preview'] else '关'}")
    else:
        print(f"  内置音色: {config['voice']}")
        print(f"  风格提示: {config['style']}")
    print(f"  音频标签: {config['style_tags'] or '无'}")
    print(f"  输出目录: {config['output_dir']}")
    print(f"  采样率: {config['sample_rate']}Hz")
    print(f"  保留原始 WAV: {'是' if config['keep_temp'] else '否'}")


def interactive_mode(args: argparse.Namespace | None = None) -> int:
    config = collect_interactive_config(args)
    print_interactive_config(config)

    if not os.environ.get("MIMO_API_KEY"):
        print("\n[WARN] 当前没有设置 MIMO_API_KEY，生成时会失败。")
        print('       PowerShell 示例: $env:MIMO_API_KEY="your_api_key"')

    print("\n输入说明:")
    print("  直接输入文本: 自动按文本生成文件名")
    print("  文本 => 文件名.wav: 指定输出文件名")
    print("  :config 重新配置，q 退出")
    print("-" * 72)

    index = 1
    while True:
        try:
            line = input("文本> ").strip()
            if not line:
                continue
            if line.lower() in ("q", "quit", "exit"):
                print("已退出。")
                return 0
            if line == ":config":
                config = collect_interactive_config(args)
                print_interactive_config(config)
                continue

            text, output_file = parse_interactive_text_line(line)
            if not text:
                print("[WARN] 文本不能为空")
                continue

            output_dir = Path(config["output_dir"])
            if output_file:
                output_path = Path(output_file).expanduser()
                if not output_path.is_absolute():
                    output_path = output_dir / output_path
                output_path = ensure_wav_suffix(output_path)
            else:
                output_path = unique_output_path(output_dir, text, index)

            generate_wav(
                text=text,
                output_file=str(output_path),
                mode=str(config["mode"]),
                voice=str(config["voice"]),
                style=str(config["style"]),
                voice_design=str(config["voice_design"]),
                style_tags=config["style_tags"],  # type: ignore[arg-type]
                model=config["model"],  # type: ignore[arg-type]
                optimize_text_preview=bool(config["optimize_text_preview"]),
                sample_rate=int(config["sample_rate"]),
                keep_temp=bool(config["keep_temp"]),
                base_url=str(config["base_url"]),
            )
            index += 1
            print()
        except KeyboardInterrupt:
            print("\n已退出。")
            return 0
        except Exception as exc:
            print(f"[ERROR] 生成失败: {exc}")
            print()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate 16kHz 16-bit mono PCM WAV via Xiaomi MiMo-V2.5-TTS."
    )
    parser.add_argument("text", nargs="?", help="Text to synthesize. Omit to enter interactive mode.")
    parser.add_argument(
        "-i",
        "--interactive",
        action="store_true",
        help="Configure once, then repeatedly input text to generate WAV files.",
    )
    parser.add_argument("-o", "--output", default="output.wav", help="Output WAV path.")
    parser.add_argument(
        "--mode",
        choices=[VOICE_DESIGN_MODE, BUILTIN_MODE],
        default=DEFAULT_MODE,
        help="TTS mode. Default: voice-design.",
    )
    parser.add_argument("--voice", default=DEFAULT_VOICE, help="MiMo built-in voice ID for builtin mode.")
    parser.add_argument(
        "--voice-design",
        default=None,
        help="Voice design description for voice-design mode.",
    )
    parser.add_argument(
        "--style",
        default=None,
        help="Builtin mode style prompt. Also works as a backward-compatible voice-design alias.",
    )
    parser.add_argument(
        "--style-tags",
        default=DEFAULT_STYLE_TAGS,
        help="MiMo audio tags prepended to assistant text. Disabled by default for exact text.",
    )
    parser.add_argument(
        "--no-style-tags",
        action="store_true",
        help="Do not prepend MiMo audio tags to the synthesized text.",
    )
    parser.add_argument("--model", default=None, help="Override MiMo TTS model name.")
    parser.set_defaults(optimize_text_preview=False)
    parser.add_argument(
        "--optimize-text-preview",
        dest="optimize_text_preview",
        action="store_true",
        help="Enable MiMo text polishing in voice-design mode. May rewrite or expand text.",
    )
    parser.add_argument(
        "--no-optimize-text-preview",
        dest="optimize_text_preview",
        action="store_false",
        help="Disable MiMo text polishing in voice-design mode. This is the default.",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help="Output sample rate. Default: 16000.",
    )
    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="Keep the raw MiMo source WAV next to the output file.",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help="MiMo OpenAI-compatible API base URL.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.interactive or args.text is None:
        return interactive_mode(args)

    builtin_style = args.style or DEFAULT_BUILTIN_STYLE
    voice_design = args.voice_design or args.style or DEFAULT_VOICE_DESIGN

    try:
        output_path = generate_wav(
            text=args.text,
            output_file=args.output,
            mode=args.mode,
            voice=args.voice,
            style=builtin_style,
            voice_design=voice_design,
            style_tags=None if args.no_style_tags else args.style_tags,
            model=args.model,
            optimize_text_preview=args.optimize_text_preview,
            sample_rate=args.sample_rate,
            keep_temp=args.keep_temp,
            base_url=args.base_url,
        )
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1

    print(f"[DONE] {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
