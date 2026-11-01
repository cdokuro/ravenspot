#!/usr/bin/env python3
"""
Generate mechanical keyboard acoustic sounds matching the Aula F75 / AU75
with an ultra-smooth, warm, velvety creamy switch profile (lubed POM stem, IXPE foam).

Acoustic Profile:
- Zero high-frequency hiss, clicks, or digital pops (subdued, rounded attack).
- Warm, deep, satisfying low-mid body resonance (230 - 270 Hz).
- Snappy, velvety decay envelope (32ms for hover, 46ms for click).
"""

import os
import wave
import numpy as np

SAMPLE_RATE = 44100
SOUNDS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sounds")

def synthesize_velvety_creamy():
    os.makedirs(SOUNDS_DIR, exist_ok=True)
    
    # ── 1. THOCK.WAV: Velvety soft marble creamy thock (Hover / Navigation) ──
    dur_thk = 0.032  # 32 ms
    t_thk = np.linspace(0, dur_thk, int(SAMPLE_RATE * dur_thk), endpoint=False)
    
    # Smooth attack envelope (1.6ms sinusoidal onset) to completely eliminate harsh high-end clicks
    att_samples_thk = int(SAMPLE_RATE * 0.0016)
    env_att_thk = np.ones_like(t_thk)
    env_att_thk[:att_samples_thk] = 0.5 * (1 - np.cos(np.pi * np.linspace(0, 1, att_samples_thk)))
    
    decay_thk = np.exp(-t_thk / 0.0085)
    f0_thk = 265.0  # Warm rounded marble thock
    body_thk = (
        0.75 * np.sin(2 * np.pi * f0_thk * t_thk) +
        0.35 * np.sin(2 * np.pi * (f0_thk * 1.58) * t_thk + 0.4) +
        0.20 * np.sin(2 * np.pi * 175.0 * t_thk)
    )
    
    sig_thk = body_thk * env_att_thk * decay_thk
    fade_thk = np.minimum(1.0, (dur_thk - t_thk) / 0.004)
    sig_thk = sig_thk * np.maximum(0.0, fade_thk)
    sig_thk = (sig_thk / np.max(np.abs(sig_thk))) * 0.85
    pcm_thock = (sig_thk * 32767).astype(np.int16)

    # ── 2. CREAMY.WAV: Deeper, warm creamy keypress bottom-out (Click / Select) ──
    dur_crm = 0.046  # 46 ms
    t_crm = np.linspace(0, dur_crm, int(SAMPLE_RATE * dur_crm), endpoint=False)
    
    att_samples_crm = int(SAMPLE_RATE * 0.0018)
    env_att_crm = np.ones_like(t_crm)
    env_att_crm[:att_samples_crm] = 0.5 * (1 - np.cos(np.pi * np.linspace(0, 1, att_samples_crm)))
    
    decay_crm = np.exp(-t_crm / 0.012)
    f0_crm = 230.0  # Deeper creamy body
    body_crm = (
        0.80 * np.sin(2 * np.pi * f0_crm * t_crm) +
        0.40 * np.sin(2 * np.pi * (f0_crm * 1.62) * t_crm + 0.3) +
        0.30 * np.sin(2 * np.pi * 150.0 * t_crm)
    )
    
    sig_crm = body_crm * env_att_crm * decay_crm
    fade_crm = np.minimum(1.0, (dur_crm - t_crm) / 0.004)
    sig_crm = sig_crm * np.maximum(0.0, fade_crm)
    sig_crm = (sig_crm / np.max(np.abs(sig_crm))) * 0.85
    pcm_creamy = (sig_crm * 32767).astype(np.int16)
    
    thock_path = os.path.join(SOUNDS_DIR, "thock.wav")
    creamy_path = os.path.join(SOUNDS_DIR, "creamy.wav")
    
    with wave.open(thock_path, "w") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SAMPLE_RATE)
        f.writeframes(pcm_thock.tobytes())
        
    with wave.open(creamy_path, "w") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SAMPLE_RATE)
        f.writeframes(pcm_creamy.tobytes())

    print(f"Synthesized velvety creamy switch sounds successfully:")
    print(f"  {thock_path} ({len(pcm_thock)} frames, {(len(pcm_thock)/SAMPLE_RATE)*1000:.1f}ms)")
    print(f"  {creamy_path} ({len(pcm_creamy)} frames, {(len(pcm_creamy)/SAMPLE_RATE)*1000:.1f}ms)")

if __name__ == "__main__":
    synthesize_velvety_creamy()
