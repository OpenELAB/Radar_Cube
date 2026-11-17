"""
Text-to-speech tool for 16 kHz, 16-bit mono PCM WAV files.
Edge TTS (based on Microsoft Edge).
Automatically locates ffmpeg.
"""
import asyncio
import os
import re
import sys
import subprocess
import wave
from pathlib import Path


# ffmpeg search path
FFMPEG_PATHS = [
    r"C:\Users\zhangyu\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe",
    r"C:\Program Files\FFmpeg\bin\ffmpeg.exe",
    r"C:\ffmpeg\bin\ffmpeg.exe",
]

# Get the directory where the script is located. All files are saved in this directory by default.
SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIR = SCRIPT_DIR / "data"


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


def prompt_with_default(label: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default not in (None, "") else ""
    value = input(f"{label}{suffix}: ").strip()
    return value if value else (default or "")


def prompt_int(label: str, default: int) -> int:
    while True:
        value = prompt_with_default(label, str(default))
        try:
            return int(value)
        except ValueError:
            print("[WARN] Enter an integer")


def prompt_yes_no(label: str, default: bool = True) -> bool:
    default_text = "y" if default else "n"
    while True:
        value = prompt_with_default(f"{label} (y/n)", default_text).lower()
        if value in ("y", "yes"):
            return True
        if value in ("n", "no"):
            return False
        print("[WARN] Enter y or n")


def setup_ffmpeg():
    """
    Locate ffmpeg automatically.
    Return the path to the ffmpeg executable.
    """
    # 1. Try to find from PATH
    try:
        result = subprocess.run(
            ["where", "ffmpeg"],
            capture_output=True,
            text=True,
            shell=True
        )
        if result.returncode == 0 and result.stdout.strip():
            print("[OK] Found ffmpeg on PATH")
            return "ffmpeg"
    except:
        pass
    
    # 2. Try predefined paths
    for path in FFMPEG_PATHS:
        if Path(path).exists():
            print(f"[OK] Found ffmpeg: {path}")
            return path
    
    print("[WARN] ffmpeg was not found. Install it manually:")
    print("  1. winget install Gyan.FFmpeg")
    print("  2. Restart the terminal")
    print("  Or run: powershell -ExecutionPolicy Bypass -File add_ffmpeg_to_path.ps1")
    return None


async def generate_wav(
    text: str,
    output_file: str = "output.wav",
    voice: str = "zh-CN-XiaoxiaoNeural",
    sample_rate: int = 16000,
    channels: int = 1,
    bits_per_sample: int = 16,
    cleanup_mp3: bool = True
):
    """
    Generate a WAV file in the specified PCM format.
    
    Args:
        text: Text to convert
        output_file: Output WAV filename
        voice: Voice ID
        sample_rate: Sample rate (default: 16000 Hz)
        channels: Channel count (default: 1, mono)
        bits_per_sample: Bit depth (default: 16)
        cleanup_mp3: Whether to remove temporary MP3 files
    """
    import edge_tts
    
    ffmpeg_path = setup_ffmpeg()
    if not ffmpeg_path:
        sys.exit(1)
    
    # Convert the output file path to an absolute path in the script directory
    output_path = Path(output_file)
    if not output_path.is_absolute():
        output_path = SCRIPT_DIR / output_file
    
    # Temporary MP3 file (saved in script directory)
    mp3_file = output_path.with_suffix('.mp3')
    
    print(f"[TTS] Generating speech: '{text}'")
    print(f"      Voice: {voice}")
    print(f"      Sample rate: {sample_rate} Hz")
    print(f"      Bit depth: {bits_per_sample} bits")
    print(f"      Channels: {channels}")
    print(f"      Output directory: {output_path.parent}")
    
    # 1. Use edge-tts to generate MP3
    communicate = edge_tts.Communicate(text, voice)
    await communicate.save(str(mp3_file))
    print(f"[OK] MP3 generated: {mp3_file.name}")
    
    # Temporary PCM file (saved in the script directory)
    pcm_file = output_path.with_suffix('.pcm')
    
    # 2. Convert to PCM using ffmpeg
    cmd = [
        ffmpeg_path,
        '-i', str(mp3_file),
        '-f', 's16le',              # Output format: signed 16-bit little-endian
        '-acodec', 'pcm_s16le',     # PCM 16-bit
        '-ar', str(sample_rate),    # Sample rate
        '-ac', str(channels),       # Channel count
        '-y',                       # Overwrite output file
        str(pcm_file)
    ]
    
    print(f"[CONV] Converting to PCM...")
    
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding='utf-8',
        errors='replace'
    )
    
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg conversion failed:\n{result.stderr}")
    
    # Verify PCM file
    if not pcm_file.exists():
        raise RuntimeError("PCM file was not generated")
    
    print(f"[OK] PCM generated: {pcm_file.name}")
    
    # 3. Encapsulate PCM data into WAV file
    print(f"[WAV] Packaging PCM data as WAV...")
    
    # Read PCM data
    with open(pcm_file, 'rb') as f:
        pcm_data = f.read()
    
    # Create WAV file
    with wave.open(str(output_path), 'wb') as wav_file:
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(bits_per_sample // 8)  # 16bit = 2 bytes
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_data)
    
    # Verify output
    file_size = output_path.stat().st_size
    duration = len(pcm_data) / (sample_rate * channels * (bits_per_sample // 8))
    
    print(f"[OK] WAV generated: {output_path.name}")
    print(f"      Full path: {output_path}")
    print(f"      File size: {file_size} bytes ({file_size/1024:.2f} KB)")
    print(f"      Duration: {duration:.2f} s")
    print(f"      Format: PCM {sample_rate} Hz, {bits_per_sample} bit, {channels} channels")
    
    # Clean up temporary files
    if cleanup_mp3:
        mp3_file.unlink(missing_ok=True)
        pcm_file.unlink(missing_ok=True)
        print(f"[OK] Temporary files removed")


async def interactive_mode():
    """
    Interactive mode: accept text and generate speech.
    """
    print("=" * 60)
    print("  Edge TTS text-to-speech tool (WAV output)")
    print("  Generates 16 kHz, 16-bit mono PCM WAV files")
    print("=" * 60)
    
    # Set up ffmpeg
    if not setup_ffmpeg():
        return
    
    print("\nCommon Chinese voices:")
    print("  [Mandarin]")
    print("    zh-CN-XiaoxiaoNeural  (female, warm and friendly; recommended)")
    print("    zh-CN-XiaoyiNeural    (female, gentle)")
    print("    zh-CN-YunxiNeural     (male, youthful)")
    print("    zh-CN-YunyangNeural   (male, steady)")
    print("    zh-CN-YunjianNeural   (male, announcer style)")
    print("    zh-CN-YunxiaNeural    (male, composed)")
    print("    zh-CN-XiaochenNeural  (female, gentle)")
    print("    zh-CN-XiaohanNeural   (female, sweet)")
    print("    zh-CN-XiaomengNeural  (female, playful)")
    print("  [Regional dialects]")
    print("    zh-CN-liaoning-XiaobeiNeural  (Northeastern Chinese)")
    print("    zh-CN-shaanxi-XiaoniNeural    (Shaanxi dialect)")
    print("  [Cantonese / Taiwan Mandarin]")
    print("    zh-HK-WanLungNeural   (male Cantonese)")
    print("    zh-TW-HsiaoYuNeural   (Taiwan Mandarin)")
    print("\n  List all voices with: edge-tts --list-voices\n")
    
    voice = prompt_with_default("Voice", "zh-CN-XiaoxiaoNeural")
    output_dir = Path(prompt_with_default("Output directory", str(DEFAULT_OUTPUT_DIR))).expanduser()
    sample_rate = prompt_int("Sample rate", 16000)
    cleanup_mp3 = prompt_yes_no("Remove temporary MP3/PCM files", True)

    print("\nInput:")
    print("  Enter text to generate a filename automatically")
    print("  text => filename.wav to specify the output filename")
    print("  :config to reconfigure; q to quit")
    print("-" * 60)

    index = 1
    while True:
        try:
            line = input("Text> ").strip()
            
            if line.lower() in ("q", "quit", "exit"):
                print("\nGoodbye!")
                break

            if line == ":config":
                voice = prompt_with_default("Voice", voice)
                output_dir = Path(prompt_with_default("Output directory", str(output_dir))).expanduser()
                sample_rate = prompt_int("Sample rate", sample_rate)
                cleanup_mp3 = prompt_yes_no("Remove temporary MP3/PCM files", cleanup_mp3)
                print("-" * 60)
                continue
            
            text, output_file = parse_interactive_text_line(line)
            if not text:
                print("[WARN] Text cannot be empty!")
                continue

            if output_file:
                output_path = Path(output_file).expanduser()
                if not output_path.is_absolute():
                    output_path = output_dir / output_path
                output_path = ensure_wav_suffix(output_path)
            else:
                output_path = unique_output_path(output_dir, text, index)
            
            print()
            await generate_wav(
                text=text,
                output_file=str(output_path),
                voice=voice,
                sample_rate=sample_rate,
                cleanup_mp3=cleanup_mp3
            )
            index += 1
            print("\n" + "-" * 60 + "\n")
            
        except KeyboardInterrupt:
            print("\n\nCancelled")
            break
        except Exception as e:
            print(f"\n[ERROR] Generation failed: {e}")
            import traceback
            traceback.print_exc()
            print()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Command line mode
        text = " ".join(sys.argv[1:])
        
        async def main():
            await generate_wav(text)
        
        asyncio.run(main())
    else:
        # Interactive mode
        asyncio.run(interactive_mode())
