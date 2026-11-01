#!/usr/bin/env python3
"""
Generate mechanical keyboard acoustic sounds matching the Aula F75 / AU75
equipped with LEOBOG Seiya Linear Switches.

Acoustic Profile:
- Gasket-mounted PC plate with 5 layers of acoustic foam (IXPE sheet + PORON)
- Factory-lubed POM linear stem on nylon housing
- Signature "marble creamy pop" on bottom out (320 - 420 Hz body with 560 - 880 Hz IXPE pop)
- Zero hollow rattle, zero artificial sine frequency drops
- Snappy, damped transient decay (~40ms for hover tap, ~55ms for full keypress)
"""

import os
import wave
import numpy as np

SAMPLE_RATE = 44100
SOUNDS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sounds")

def synthesize_aula_seiya():
    os.makedirs(SOUNDS_DIR, exist_ok=True)
    
    # ── 1. THOCK.WAV: Aula AU75 Seiya light switch bottom-out / marble tap (Hover / Navigation) ──
    dur_thock = 0.040  # 40 ms
    t_thk = np.linspace(0, dur_thock, int(SAMPLE_RATE * dur_thock), endpoint=False)
    
    np.random.seed(1337)
    noise_thk = np.random.normal(0, 1, len(t_thk))
    
    # A. Initial POM stem tap transient (lubed nylon contact, 1.8 - 3.2 kHz)
    transient_thk = noise_thk * (0.6 * np.sin(2 * np.pi * 2400 * t_thk) + 0.4 * np.sin(2 * np.pi * 1750 * t_thk)) * np.exp(-t_thk / 0.0022)
    
    # B. The famous Aula F75 "Marble Pop" (IXPE sheet + PET film)
    f0 = 345.0  # Main marble pop frequency
    body_thk1 = 0.85 * np.sin(2 * np.pi * f0 * t_thk) * np.exp(-t_thk / 0.012)
    body_thk2 = 0.55 * np.sin(2 * np.pi * (f0 * 1.78) * t_thk + 0.4) * np.exp(-t_thk / 0.009)
    body_thk3 = 0.35 * np.sin(2 * np.pi * (f0 * 2.85) * t_thk + 0.9) * np.exp(-t_thk / 0.006)
    
    # C. Gasket & Poron damping (deep, warm, zero hollow rattle)
    gasket_thk = 0.40 * np.sin(2 * np.pi * 215 * t_thk) * np.exp(-t_thk / 0.011)
    
    sig_thock = 0.50 * transient_thk + body_thk1 + body_thk2 + body_thk3 + gasket_thk
    
    # Gentle taper to prevent any DC edge
    fade_thk = np.minimum(1.0, (dur_thock - t_thk) / 0.005)
    sig_thock = sig_thock * np.maximum(0.0, fade_thk)
    sig_thock = (sig_thock / np.max(np.abs(sig_thock))) * 0.88
    pcm_thock = (sig_thock * 32767).astype(np.int16)

    # ── 2. CREAMY.WAV: Aula AU75 Seiya full keypress bottom-out (Click / Select) ──
    dur_crm = 0.055  # 55 ms
    t_crm = np.linspace(0, dur_crm, int(SAMPLE_RATE * dur_crm), endpoint=False)
    
    np.random.seed(2024)
    noise_crm = np.random.normal(0, 1, len(t_crm))
    
    # A. Initial impact transient
    transient_crm = noise_crm * (0.7 * np.sin(2 * np.pi * 2100 * t_crm) + 0.3 * np.sin(2 * np.pi * 1450 * t_crm)) * np.exp(-t_crm / 0.0028)
    
    # B. Creamy acoustic body resonance (fuller bottom-out)
    fc = 310.0
    body_crm1 = 0.95 * np.sin(2 * np.pi * fc * t_crm) * np.exp(-t_crm / 0.017)
    body_crm2 = 0.60 * np.sin(2 * np.pi * (fc * 1.68) * t_crm + 0.3) * np.exp(-t_crm / 0.013)
    body_crm3 = 0.35 * np.sin(2 * np.pi * (fc * 2.55) * t_crm + 0.7) * np.exp(-t_crm / 0.008)
    gasket_crm = 0.50 * np.sin(2 * np.pi * 185 * t_crm) * np.exp(-t_crm / 0.015)
    
    # C. Switch leaf activation tick around 13ms
    t_tick = t_crm - 0.013
    tick_mask = t_tick > 0
    leaf_tick = np.zeros_like(t_crm)
    leaf_tick[tick_mask] = 0.25 * noise_crm[tick_mask] * np.sin(2 * np.pi * 2600 * t_tick[tick_mask]) * np.exp(-t_tick[tick_mask] / 0.002)
    leaf_res = np.zeros_like(t_crm)
    leaf_res[tick_mask] = 0.25 * np.sin(2 * np.pi * 480 * t_tick[tick_mask]) * np.exp(-t_tick[tick_mask] / 0.007)
    
    sig_creamy = 0.55 * transient_crm + body_crm1 + body_crm2 + body_crm3 + gasket_crm + leaf_tick + leaf_res
    fade_crm = np.minimum(1.0, (dur_crm - t_crm) / 0.006)
    sig_creamy = sig_creamy * np.maximum(0.0, fade_crm)
    sig_creamy = (sig_creamy / np.max(np.abs(sig_creamy))) * 0.90
    pcm_creamy = (sig_creamy * 32767).astype(np.int16)
    
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

    print(f"Synthesized Aula AU75 Seiya sounds successfully:")
    print(f"  {thock_path} ({len(pcm_thock)} frames, {(len(pcm_thock)/SAMPLE_RATE)*1000:.1f}ms)")
    print(f"  {creamy_path} ({len(pcm_creamy)} frames, {(len(pcm_creamy)/SAMPLE_RATE)*1000:.1f}ms)")

if __name__ == "__main__":
    synthesize_aula_seiya()
