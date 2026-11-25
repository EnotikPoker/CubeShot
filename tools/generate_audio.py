#!/usr/bin/env python3
"""Reproduce CubeShot's original sound bank. No third-party recordings or packages."""
import json
import math
from pathlib import Path
import random
import struct
import wave

RATE = 48000
ROOT = Path(__file__).resolve().parents[1] / "assets" / "audio"
# duration, fundamental, noise, pitch drop, body decay. Guns combine a short
# mechanical transient, filtered air and a low body; melee uses rising swishes.
GUNS = {
    "pistol": (.19, 160, .8, 1.4, 25), "shotgun": (.38, 78, 1, 1.8, 13),
    "automatic": (.14, 135, .85, 1.2, 32), "smg": (.10, 240, .8, 1.5, 40),
    "revolver": (.28, 110, .9, 1.4, 17), "rifle": (.30, 100, .85, 1.3, 18),
    "sniper": (.48, 62, .9, 1.5, 11), "minigun": (.085, 180, .85, 1.1, 48),
    "rpg": (.50, 68, .95, .5, 10), "flamethrower": (.11, 60, 1, .1, 28),
    "bat": (.20, 115, .55, -.7, 18), "katana": (.19, 650, .8, -.5, 24),
    "knife": (.10, 920, .7, -.4, 40), "boomerang": (.25, 430, .6, -.5, 14),
    "grenade_launcher": (.26, 85, .5, 1.1, 19), "crossbow": (.19, 330, .4, 1.4, 24),
    "electric": (.28, 130, .35, -1, 13), "beam": (.065, 440, .1, .05, 20),
    "disc": (.24, 260, .5, -.8, 18), "hammer": (.33, 60, .6, -.5, 13),
    "sticky": (.20, 210, .4, 1.3, 23),
    "step": (.07, 120, .9, .5, 65), "jump": (.13, 200, .35, -.7, 28),
    "land": (.16, 74, .8, .4, 28), "reload": (.22, 430, .55, .1, 35),
    "shell": (.09, 600, .4, .2, 50), "ready": (.13, 800, .25, -.4, 35),
    "spin": (.16, 110, .3, -.6, 15), "overheat": (.38, 180, .7, .5, 10),
    "charge": (.22, 330, .12, -1.2, 8), "return": (.16, 570, .2, .5, 22),
    "flesh": (.11, 95, .8, .8, 40), "metal": (.22, 1350, .45, .05, 25),
    "wood": (.15, 210, .9, .3, 36), "stone": (.17, 160, .9, .5, 32),
    "glass": (.29, 2400, .8, .1, 22), "shield": (.19, 1000, .05, .8, 18),
    "hit_confirm": (.065, 1150, .08, .3, 50), "kill_confirm": (.26, 880, .04, -.6, 13),
    "explosion": (.80, 45, 1, 1.5, 6), "break": (.30, 100, 1, .7, 18),
    "death": (.27, 120, .65, 1.3, 16), "cast": (.20, 460, .15, -.5, 18),
    "heal": (.38, 660, .02, -.5, 9), "blink": (.20, 820, .2, 1.5, 17),
    "parry": (.18, 1500, .05, .25, 18), "ice": (.28, 1600, .25, -.4, 15),
    "mine": (.17, 390, .2, -.4, 22), "screen": (.35, 180, .05, -.7, 10),
    "boost": (.32, 220, .18, -1, 12), "cleanse": (.35, 1100, .15, -.3, 10),
    "warning": (.085, 1100, .02, 0, 28), "fire": (.16, 70, 1, .1, 25),
    "jetpack": (.16, 85, .9, -.1, 20), "click": (.055, 700, .04, -.2, 70),
    "back": (.08, 500, .04, .7, 45), "countdown": (.12, 600, .01, 0, 30),
    "round_start": (.40, 540, .02, -.8, 9), "round_end": (.42, 620, .02, .7, 8),
    "roll": (.04, 1200, .05, .1, 90), "reward": (.50, 660, .02, -.8, 8),
}


def generate(name, params, seed):
    duration, fundamental, noise_mix, drop, decay = params
    rng = random.Random(seed)
    phase = 0
    low = previous = 0
    samples = []
    for i in range(round(duration * RATE)):
        t = i / RATE
        phase += 2 * math.pi * fundamental * max(.25, 1 - drop * t / duration) / RATE
        noise = rng.uniform(-1, 1)
        low += .18 * (noise - low)
        high = noise - previous
        previous = noise
        body = math.sin(phase) + .2 * math.sin(phase * 2.013)
        air = .7 * low + .2 * high
        envelope = min(1, t / .0015) * math.exp(-t * decay) * min(1, (duration-t) / .015)
        if name in ("reload", "shell", "ready"):
            envelope += .35 * math.exp(-abs(t-duration*.65)*140)
        value = ((1-noise_mix*.55)*body + noise_mix*air) * envelope
        samples.append(value)
    peak = max(abs(x) for x in samples) or 1
    # quieter footsteps/UI, fuller gun transients, headroom is retained in mixer.
    level = .42 if name in ("step", "click", "roll", "fire", "jetpack", "beam") else .72
    return b"".join(struct.pack("<h", round(x/peak * level * 32767)) for x in samples)


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    for seed, (name, params) in enumerate(GUNS.items(), 2300):
        with wave.open(str(ROOT / (name + ".wav")), "wb") as output:
            output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            output.writeframes(generate(name, params, seed))
    (ROOT / "manifest.json").write_text(json.dumps({"version": 1, "clips": sorted(GUNS)}, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(GUNS)} original mono PCM sound cues in {ROOT}")


if __name__ == "__main__":
    main()
