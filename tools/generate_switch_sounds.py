#!/usr/bin/env python3
"""
Generate ultra-soft, whisper-quiet UI acoustics matching the Apple iOS / iPhone keyboard
soft haptic click profile (Tock / Keypress Click).

Acoustic Profile:
- Ultra-short duration: 16-24ms total duration.
- Zero boomy bass, zero hollow drumming "tok-tok" resonance.
- Soft, rounded, non-fatiguing transient tap.
- Whispering tactile haptic feedback perfect for rapid menu browsing and mouse hovers.
"""

import os
import wave
import numpy as np

SAMPLE_RATE = 44100
SOUNDS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sounds")

def synthesize_ios_soft_haptic():
    os.makedirs(SOUNDS_DIR, exist_ok=True)
    
    # ── 1. THOCK.WAV: iPhone iOS soft keyboard tap (Hover / Navigation) ──
    dur_thk = 0.018  # 18 ms
    t_thk = np.linspace(0, dur_thk, int(SAMPLE_RATE * dur_thk), endpoint=False)
    
    # Rounded attack (0.8ms) to prevent digital harshness
    att_samples_thk = int(SAMPLE_RATE * 0.0008)
    env_att_thk = np.ones_like(t_thk)
    env_att_thk[:att_samples_thk] = 0.5 * (1 - np.cos(np.pi * np.linspace(0, 1, att_samples_thk)))
    
    # Very fast, crisp, airy decay
    decay_thk = np.exp(-t_thk / 0.0032)
    
    # iOS keypress click acoustic profile: delicate airy tap + subtle rounded wood body
    tap_thk = (
        0.55 * np.sin(2 * np.pi * 1850 * t_thk) +
        0.35 * np.sin(2 * np.pi * 1250 * t_thk + 0.3) +
        0.25 * np.sin(2 * np.pi * 520 * t_thk + 0.6)
    )
    
    sig_thk = tap_thk * env_att_thk * decay_thk
    fade_thk = np.minimum(1.0, (dur_thk - t_thk) / 0.002)
    sig_thk = sig_thk * np.maximum(0.0, fade_thk)
    sig_thk = (sig_thk / np.max(np.abs(sig_thk))) * 0.65
    pcm_thock = (sig_thk * 32767).astype(np.int16)

    # ── 2. CREAMY.WAV: iPhone iOS Tock / Keypress select (Click / Activation) ──
    dur_crm = 0.024  # 24 ms
    t_crm = np.linspace(0, dur_crm, int(SAMPLE_RATE * dur_crm), endpoint=False)
    
    att_samples_crm = int(SAMPLE_RATE * 0.001)
    env_att_crm = np.ones_like(t_crm)
    env_att_crm[:att_samples_crm] = 0.5 * (1 - np.cos(np.pi * np.linspace(0, 1, att_samples_crm)))
    
    decay_crm = np.exp(-t_crm / 0.0048)
    tap_crm = (
        0.60 * np.sin(2 * np.pi * 1450 * t_crm) +
        0.35 * np.sin(2 * np.pi * 920 * t_crm + 0.4) +
        0.30 * np.sin(2 * np.pi * 420 * t_crm + 0.8)
    )
    
    sig_crm = tap_crm * env_att_crm * decay_crm
    fade_crm = np.minimum(1.0, (dur_crm - t_crm) / 0.003)
    sig_crm = sig_crm * np.maximum(0.0, fade_crm)
    sig_crm = (sig_crm / np.max(np.abs(sig_crm))) * 0.75
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

    print(f"Generated soft iOS haptic sounds:")
    print(f"  {thock_path} ({len(pcm_thock)} frames, {(len(pcm_thock)/SAMPLE_RATE)*1000:.1f}ms)")
    print(f"  {creamy_path} ({len(pcm_creamy)} frames, {(len(pcm_creamy)/SAMPLE_RATE)*1000:.1f}ms)")

if __name__ == "__main__":
    synthesize_ios_soft_haptic()
