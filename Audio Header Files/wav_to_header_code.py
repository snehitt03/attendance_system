import soundfile as sf
import numpy as np
import os

# ---------------- CONFIG ----------------
WAV_FILES = [
    "checkin_success.wav",
    "checkout_success.wav",
    "enrollment_success.wav",
    "verification_invalid.wav",
    "verification_valid.wav"
]

OUTPUT_DIR = "D:\QUANTUMCLK\Atendence_system"
TARGET_SR = 16000   # 16kHz
MAX_AMPLITUDE = 32767

os.makedirs(OUTPUT_DIR, exist_ok=True)

# ----------------------------------------
# replace convert_wav_to_header in your code B with this

from scipy.signal import resample_poly

TARGET_SR = 16000    # IMPORTANT: match your ESP32 SAMPLE_RATE
MAX_AMPLITUDE = 32767
HEADROOM = 0.90      # keep headroom to avoid clipping (0.8..0.95 reasonable)
FADE_MS = 10         # fade-in/out to avoid clicks (milliseconds)

def convert_wav_to_header(wav_file):
    data, samplerate = sf.read(wav_file, dtype='float32')

    # Convert to mono
    if data.ndim > 1:
        data = np.mean(data, axis=1)

    # Remove DC offset
    data = data - np.mean(data)

    # If the file is all zeros (silent), guard against divide-by-zero
    peak = np.max(np.abs(data))
    if peak < 1e-9:
        data = data * 0.0
    else:
        # Normalize to -1..1 then apply headroom
        data = (data / peak) * HEADROOM

    # Resample using polyphase (anti-alias). Choose rational factors:
    # resample_poly(x, up, down) -> new_sr = samplerate * up / down
    # We'll compute up/down from samplerate and TARGET_SR:
    from math import gcd
    up = TARGET_SR
    down = samplerate
    g = gcd(up, down)
    up //= g
    down //= g

    if samplerate != TARGET_SR:
        data = resample_poly(data, up, down)

    # Apply short fade in/out to remove clicks (FADE_MS)
    fade_len = int((FADE_MS / 1000.0) * TARGET_SR)
    if fade_len > 0 and len(data) > 2*fade_len:
        # linear ramps
        fade_in = np.linspace(0.0, 1.0, fade_len)
        fade_out = np.linspace(1.0, 0.0, fade_len)
        data[:fade_len] *= fade_in
        data[-fade_len:] *= fade_out

    # Convert to int16
    pcm_data = (data * MAX_AMPLITUDE).astype(np.int16)

    name = os.path.splitext(os.path.basename(wav_file))[0]
    header_file = os.path.join(OUTPUT_DIR, f"{name}.h")

    with open(header_file, "w") as f:
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n")
        #f.write("#include <avr/pgmspace.h>\n\n")  # keep PROGMEM for microcontrollers (optional)
        f.write(f"const int16_t {name}[] PROGMEM = {{\n")

        for i, sample in enumerate(pcm_data):
            f.write(f"{int(sample)}, ")
            if i % 12 == 11:
                f.write("\n")

        f.write("\n};\n\n")
        f.write(f"const unsigned int {name}_len = {len(pcm_data)};\n")

    print(f"Generated: {header_file}")

# ----------------------------------------
for wav in WAV_FILES:
    convert_wav_to_header(wav)
