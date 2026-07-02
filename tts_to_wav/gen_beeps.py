"""
基于 "18 - C6 1046.5hz Chime.wav" 生成倒车雷达蜂鸣音频
使用 ffmpeg 高质量重采样: 48kHz/24bit → 16kHz/16bit Mono PCM WAV
"""
import subprocess
import numpy as np
import wave
import os

SRC_FILE = "18 - C6 1046.5hz Chime.wav"
OUT_DIR = "data"
TARGET_RATE = 16000
TARGET_WIDTH = 2  # 16bit

# ffmpeg 路径
FFMPEG = r"C:\Users\zhangyu\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe"


def read_wav(path):
    """读取 WAV 返回 (samples, rate)"""
    with wave.open(path, 'rb') as w:
        rate = w.getframerate()
        n = w.getnframes()
        raw = w.readframes(n)
        samples = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
    return samples, rate


def write_wav(path, samples, rate=TARGET_RATE):
    """写入 WAV"""
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(TARGET_WIDTH)
        w.setframerate(rate)
        clipped = np.clip(samples, -32768, 32767).astype(np.int16)
        w.writeframes(clipped.tobytes())
    duration = len(samples) / rate
    size = len(samples) * TARGET_WIDTH
    print(f"  → {path}: {size} bytes, {duration:.3f}s")


def ffmpeg_resample(src_path, dst_path):
    """使用 ffmpeg 高质量重采样到 16kHz/16bit/mono"""
    cmd = [
        FFMPEG,
        '-i', src_path,
        '-ar', str(TARGET_RATE),
        '-ac', '1',
        '-sample_fmt', 's16',
        '-y',
        dst_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg 重采样失败:\n{result.stderr}")


def speed_up(samples, factor):
    """变速 + 变调: 加速 factor 倍 (取前 len/factor 样本)"""
    n = int(len(samples) / factor)
    return samples[:n]


def apply_fade(samples, rate, fade_in_ms=1.0, fade_out_ms=2.5):
    """
    对音频片段应用淡入淡出，消除拼接处的爆破音(click/pop)。
    fade_in:  从 0 线性过渡到 1
    fade_out: 从 1 线性过渡到 0
    """
    result = samples.copy()
    n = len(samples)

    fade_in_samples = min(int(rate * fade_in_ms / 1000), n // 2)
    fade_out_samples = min(int(rate * fade_out_ms / 1000), n // 2)

    if fade_in_samples > 0:
        ramp = np.linspace(0.0, 1.0, fade_in_samples, dtype=np.float64)
        result[:fade_in_samples] *= ramp

    if fade_out_samples > 0:
        ramp = np.linspace(1.0, 0.0, fade_out_samples, dtype=np.float64)
        result[-fade_out_samples:] *= ramp

    return result


print("=" * 50)
print("倒车雷达蜂鸣音频生成 (ffmpeg 重采样)")
print("=" * 50)

# 1. ffmpeg 重采样源文件
print(f"\n[1/3] ffmpeg 重采样: {SRC_FILE}")
tmp_file = "chime_16k.wav"
ffmpeg_resample(SRC_FILE, tmp_file)

chime, rate = read_wav(tmp_file)
print(f"  重采样后: {rate}Hz, {len(chime)} frames, {len(chime)/rate:.3f}s")

# 2. BeepShort — 单次滴声原速
print(f"\n[2/3] 生成音频文件")
print(f"  [BeepShort] 单次滴声，原速")
chime_short = apply_fade(chime, TARGET_RATE, fade_in_ms=1.0, fade_out_ms=3.0)
write_wav(f"{OUT_DIR}/beep_short.wav", chime_short)

# 3. BeepDanger — 4连滴，间隔25ms，1.5x加速
print(f"  [BeepDanger] 4连滴，间隔25ms，1.5x加速")
chime_fast = speed_up(chime, 1.5)
chime_fast = apply_fade(chime_fast, TARGET_RATE, fade_in_ms=1.0, fade_out_ms=2.5)

gap_samples = int(TARGET_RATE * 0.025)  # 25ms
silence = np.zeros(gap_samples, dtype=np.float64)

parts = []
for i in range(4):
    if i > 0:
        parts.append(silence)
    parts.append(chime_fast)
danger = np.concatenate(parts)
write_wav(f"{OUT_DIR}/beep_danger.wav", danger)

# 清理临时文件
os.unlink(tmp_file)

print(f"\n蜂鸣音频生成完成！")
