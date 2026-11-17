"""
Generate reversing radar beeps from "18 - C6 1046.5hz Chime.wav".
Use ffmpeg for high-quality resampling: 48 kHz/24-bit to 16 kHz/16-bit mono PCM WAV.
"""
import subprocess
import numpy as np
import wave
import os

SRC_FILE = "18 - C6 1046.5hz Chime.wav"
OUT_DIR = "data"
TARGET_RATE = 16000
TARGET_WIDTH = 2  # 16bit

# ffmpeg path
FFMPEG = r"C:\Users\zhangyu\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe"


def read_wav(path):
    """Read a WAV file and return (samples, rate)."""
    with wave.open(path, 'rb') as w:
        rate = w.getframerate()
        n = w.getnframes()
        raw = w.readframes(n)
        samples = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
    return samples, rate


def write_wav(path, samples, rate=TARGET_RATE):
    """Write a WAV file."""
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
    """Use ffmpeg to resample to 16 kHz, 16-bit mono."""
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
        raise RuntimeError(f"ffmpeg resampling failed:\n{result.stderr}")


def speed_up(samples, factor):
    """Increase speed and pitch by factor (take the first len/factor samples)."""
    n = int(len(samples) / factor)
    return samples[:n]


def apply_fade(samples, rate, fade_in_ms=1.0, fade_out_ms=2.5):
    """
    Apply a fade-in and fade-out to prevent clicks at clip boundaries.
    fade_in:  linear transition from 0 to 1
    fade_out: linear transition from 1 to 0
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
print("Reversing radar beep generation (ffmpeg resampling)")
print("=" * 50)

# 1. ffmpeg resampling source file
print(f"\n[1/3] ffmpeg resampling: {SRC_FILE}")
tmp_file = "chime_16k.wav"
ffmpeg_resample(SRC_FILE, tmp_file)

chime, rate = read_wav(tmp_file)
print(f"  Resampled: {rate} Hz, {len(chime)} frames, {len(chime)/rate:.3f} s")

# 2. BeepShort — Single beep original speed
print(f"\n[2/3] Generating audio files")
print(f"  [BeepShort] Single beep at original speed")
chime_short = apply_fade(chime, TARGET_RATE, fade_in_ms=1.0, fade_out_ms=3.0)
write_wav(f"{OUT_DIR}/beep_short.wav", chime_short)

# 3. BeepDanger — 4 consecutive drops, 25ms interval, 1.5x acceleration
print(f"  [BeepDanger] Four beeps, 25 ms apart, 1.5x speed")
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

# Clean up temporary files
os.unlink(tmp_file)

print(f"\nBeep generation complete!")
