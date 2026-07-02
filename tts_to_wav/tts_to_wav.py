"""
文本转语音 - 生成 16kHz 16bit 单声道 PCM 编码的 WAV 音频文件
Edge TTS (免费，基于 Microsoft Edge)
(自动处理 ffmpeg 路径)
"""
import asyncio
import os
import re
import sys
import subprocess
import wave
from pathlib import Path


# ffmpeg 搜索路径
FFMPEG_PATHS = [
    r"C:\Users\zhangyu\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe",
    r"C:\Program Files\FFmpeg\bin\ffmpeg.exe",
    r"C:\ffmpeg\bin\ffmpeg.exe",
]

# 获取脚本所在目录，所有文件默认保存在此目录下
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
            print("[WARN] 请输入整数")


def prompt_yes_no(label: str, default: bool = True) -> bool:
    default_text = "y" if default else "n"
    while True:
        value = prompt_with_default(f"{label} (y/n)", default_text).lower()
        if value in ("y", "yes"):
            return True
        if value in ("n", "no"):
            return False
        print("[WARN] 请输入 y 或 n")


def setup_ffmpeg():
    """
    自动设置 ffmpeg 路径
    返回 ffmpeg 可执行文件路径
    """
    # 1. 尝试从 PATH 查找
    try:
        result = subprocess.run(
            ["where", "ffmpeg"],
            capture_output=True,
            text=True,
            shell=True
        )
        if result.returncode == 0 and result.stdout.strip():
            print("[OK] 从 PATH 找到 ffmpeg")
            return "ffmpeg"
    except:
        pass
    
    # 2. 尝试预定义路径
    for path in FFMPEG_PATHS:
        if Path(path).exists():
            print(f"[OK] 找到 ffmpeg: {path}")
            return path
    
    print("[WARN] 未找到 ffmpeg，请手动安装:")
    print("  1. winget install Gyan.FFmpeg")
    print("  2. 重启终端")
    print("  或运行：powershell -ExecutionPolicy Bypass -File add_ffmpeg_to_path.ps1")
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
    生成指定格式的 WAV 音频文件 (PCM 编码)
    
    Args:
        text: 要转换的文本
        output_file: 输出 WAV 文件名
        voice: 语音音色
        sample_rate: 采样率 (默认 16000Hz)
        channels: 声道数 (默认 1，单声道)
        bits_per_sample: 位深 (默认 16bit)
        cleanup_mp3: 是否清理临时 MP3 文件
    """
    import edge_tts
    
    ffmpeg_path = setup_ffmpeg()
    if not ffmpeg_path:
        sys.exit(1)
    
    # 将输出文件路径转换为脚本目录下的绝对路径
    output_path = Path(output_file)
    if not output_path.is_absolute():
        output_path = SCRIPT_DIR / output_file
    
    # 临时 MP3 文件（保存在脚本目录）
    mp3_file = output_path.with_suffix('.mp3')
    
    print(f"[TTS] 正在生成语音: '{text}'")
    print(f"      音色: {voice}")
    print(f"      采样率: {sample_rate}Hz")
    print(f"      位深: {bits_per_sample}bit")
    print(f"      声道: {channels}")
    print(f"      输出路径: {output_path.parent}")
    
    # 1. 使用 edge-tts 生成 MP3
    communicate = edge_tts.Communicate(text, voice)
    await communicate.save(str(mp3_file))
    print(f"[OK] MP3 已生成: {mp3_file.name}")
    
    # 临时 PCM 文件（保存在脚本目录）
    pcm_file = output_path.with_suffix('.pcm')
    
    # 2. 使用 ffmpeg 转换为 PCM
    cmd = [
        ffmpeg_path,
        '-i', str(mp3_file),
        '-f', 's16le',              # 输出格式：signed 16-bit little-endian
        '-acodec', 'pcm_s16le',     # PCM 16-bit
        '-ar', str(sample_rate),    # 采样率
        '-ac', str(channels),       # 声道数
        '-y',                       # 覆盖输出文件
        str(pcm_file)
    ]
    
    print(f"[CONV] 正在转换为 PCM...")
    
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding='utf-8',
        errors='replace'
    )
    
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg 转换失败:\n{result.stderr}")
    
    # 验证 PCM 文件
    if not pcm_file.exists():
        raise RuntimeError("PCM 文件未生成")
    
    print(f"[OK] PCM 已生成: {pcm_file.name}")
    
    # 3. 将 PCM 数据封装为 WAV 文件
    print(f"[WAV] 正在封装为 WAV 格式...")
    
    # 读取 PCM 数据
    with open(pcm_file, 'rb') as f:
        pcm_data = f.read()
    
    # 创建 WAV 文件
    with wave.open(str(output_path), 'wb') as wav_file:
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(bits_per_sample // 8)  # 16bit = 2 bytes
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_data)
    
    # 验证输出
    file_size = output_path.stat().st_size
    duration = len(pcm_data) / (sample_rate * channels * (bits_per_sample // 8))
    
    print(f"[OK] WAV 已生成: {output_path.name}")
    print(f"      完整路径: {output_path}")
    print(f"      文件大小: {file_size} bytes ({file_size/1024:.2f} KB)")
    print(f"      时长: {duration:.2f} 秒")
    print(f"      格式: PCM {sample_rate}Hz {bits_per_sample}bit {channels}声道")
    
    # 清理临时文件
    if cleanup_mp3:
        mp3_file.unlink(missing_ok=True)
        pcm_file.unlink(missing_ok=True)
        print(f"[OK] 已清理临时文件")


async def interactive_mode():
    """
    交互模式：让用户输入文本并生成语音
    """
    print("=" * 60)
    print("  Edge TTS 文本转语音工具 (WAV格式)")
    print("  生成 16kHz 16bit 单声道 PCM 编码的 WAV 文件")
    print("=" * 60)
    
    # 设置 ffmpeg
    if not setup_ffmpeg():
        return
    
    print("\n常用中文音色:")
    print("  [普通话]")
    print("    zh-CN-XiaoxiaoNeural  (女-晓晓, 温暖亲切, 推荐)")
    print("    zh-CN-XiaoyiNeural    (女-晓伊, 柔和知性)")
    print("    zh-CN-YunxiNeural     (男-云希, 年轻活力)")
    print("    zh-CN-YunyangNeural   (男-云扬, 沉稳专业)")
    print("    zh-CN-YunjianNeural   (男-云健, 标准播音腔)")
    print("    zh-CN-YunxiaNeural    (男-云夏, 稳重可靠)")
    print("    zh-CN-XiaochenNeural  (女-晓晨, 温柔治愈)")
    print("    zh-CN-XiaohanNeural   (女-晓涵, 甜美少女音)")
    print("    zh-CN-XiaomengNeural  (女-晓萌, 软萌萝莉音)")
    print("  [方言]")
    print("    zh-CN-liaoning-XiaobeiNeural  (东北话)")
    print("    zh-CN-shaanxi-XiaoniNeural    (陕西方言)")
    print("  [粤语/台湾]")
    print("    zh-HK-WanLungNeural   (粤语男声)")
    print("    zh-TW-HsiaoYuNeural   (台湾普通话)")
    print("\n  查看完整列表命令: edge-tts --list-voices\n")
    
    voice = prompt_with_default("音色", "zh-CN-XiaoxiaoNeural")
    output_dir = Path(prompt_with_default("输出目录", str(DEFAULT_OUTPUT_DIR))).expanduser()
    sample_rate = prompt_int("采样率", 16000)
    cleanup_mp3 = prompt_yes_no("是否清理临时 MP3/PCM", True)

    print("\n输入说明:")
    print("  直接输入文本: 自动按文本生成文件名")
    print("  文本 => 文件名.wav: 指定输出文件名")
    print("  :config 重新配置，q 退出")
    print("-" * 60)

    index = 1
    while True:
        try:
            line = input("文本> ").strip()
            
            if line.lower() in ("q", "quit", "exit"):
                print("\n再见！")
                break

            if line == ":config":
                voice = prompt_with_default("音色", voice)
                output_dir = Path(prompt_with_default("输出目录", str(output_dir))).expanduser()
                sample_rate = prompt_int("采样率", sample_rate)
                cleanup_mp3 = prompt_yes_no("是否清理临时 MP3/PCM", cleanup_mp3)
                print("-" * 60)
                continue
            
            text, output_file = parse_interactive_text_line(line)
            if not text:
                print("[WARN] 文本不能为空！")
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
            print("\n\n已取消")
            break
        except Exception as e:
            print(f"\n[ERROR] 生成失败: {e}")
            import traceback
            traceback.print_exc()
            print()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        # 命令行模式
        text = " ".join(sys.argv[1:])
        
        async def main():
            await generate_wav(text)
        
        asyncio.run(main())
    else:
        # 交互模式
        asyncio.run(interactive_mode())
