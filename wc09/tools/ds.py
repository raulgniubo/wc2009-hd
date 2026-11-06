#!/usr/bin/env python3
"""Headless melonDS (libretro melonDS DS core) driver for WC2009 research.

  ds.py run --state IN.st --seq "a,w60,down*3,w30" --shot out.png --save OUT.st [--ram ram.bin]

Sequence tokens (comma separated):
  wN          idle N frames
  BTN[*K]     press button for 6 frames then release 6 frames, K times (a b x y l r start select up down left right)
  hBTN:N      hold button N frames
  tX:Y        tap touch screen at DS pixel (X,Y) on bottom screen (0-255, 0-191)
"""
import argparse, ctypes, itertools, os, sys
from pathlib import Path
from libretro import Session, JoypadState, TempDirPathDriver, Pointer
from libretro import RETRO_MEMORY_SYSTEM_RAM
from PIL import Image

CORE = os.environ.get('MDS_CORE', '/home/claude/melonds-ds/build/src/libretro/melondsds_libretro.so')
ROM = os.environ.get('WC09_ROM', '/home/claude/wc09/rom.nds')
OPTS = {
    "melonds_boot_mode": "direct",
    "melonds_console_mode": "ds",
    "melonds_sysfile_mode": "builtin",
    "melonds_render_mode": "software",
    "melonds_show_cursor": "disabled",
    "melonds_touch_mode": "touch",
}

def parse_seq(seq):
    steps = []
    for tok in [t.strip() for t in seq.split(',') if t.strip()]:
        if tok.startswith('w') and tok[1:].isdigit():
            steps += [None] * int(tok[1:])
        elif tok.startswith('h') and ':' in tok:
            b, n = tok[1:].split(':'); steps += [{b: True}] * int(n)
        elif tok.startswith('t') and ':' in tok:
            x, y = map(int, tok[1:].split(':')); steps += [('touch', x, y)] * 6 + [None] * 6
        else:
            b, k = (tok.split('*') + ['1'])[:2]
            for _ in range(int(k)):
                steps += [{b: True}] * 6 + [None] * 6
    return steps

class Inputs:
    def __init__(self): self.cur = None
inputs = Inputs()

def gen():
    while True:
        s = inputs.cur
        if isinstance(s, dict): yield JoypadState(**s)
        elif isinstance(s, tuple) and s[0] == 'touch':
            _, x, y = s
            px = int(((x + 0.5) / 256 * 2 - 1) * 0x7FFF); py = int(((192 + y + 0.5) / 384 * 2 - 1) * 0x7FFF)
            yield Pointer(px, py, True)
        else: yield JoypadState()

def save_shot(emu, path):
    shot = emu.video.screenshot()
    if shot is None: return
    mode = 'RGBX'
    img = Image.frombuffer('RGBA', (shot.width, shot.height), bytes(shot.data), 'raw', 'RGBA', 0, 1).convert('RGB')
    img.save(path)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['run'])
    ap.add_argument('--state'); ap.add_argument('--save'); ap.add_argument('--seq', default='')
    ap.add_argument('--shot'); ap.add_argument('--ram'); ap.add_argument('--shots-every', type=int, default=0)
    a = ap.parse_args()
    td = TempDirPathDriver
    with Session(CORE, ROM, input=gen, options=OPTS, path=TempDirPathDriver) as emu:
        emu.run()
        if a.state:
            data = Path(a.state).read_bytes()
            assert emu.core.unserialize(data), 'unserialize failed'
        for n, s in enumerate(parse_seq(a.seq)):
            inputs.cur = s
            emu.run()
            if a.shots_every and n % a.shots_every == 0 and a.shot:
                save_shot(emu, a.shot.replace('.png', f'_{n:05d}.png'))
        inputs.cur = None
        emu.run()
        if a.shot: save_shot(emu, a.shot)
        if a.save:
            buf = bytearray(emu.core.serialize_size()); assert emu.core.serialize(buf); Path(a.save).write_bytes(buf)
        if a.ram:
            m = emu.core.get_memory(RETRO_MEMORY_SYSTEM_RAM); Path(a.ram).write_bytes(bytes(m))

if __name__ == '__main__':
    main()
