#!/usr/bin/env python3
"""Summarise an audit run: blank detection plus a before/after contact sheet.
usage: compare.py <out-dir> [<baseline-out-dir>]
"""
import sys, os
import numpy as np
from PIL import Image

def stats(path):
    im = np.asarray(Image.open(path).convert('L'), float)
    h, w = im.shape
    c = im[h // 8:7 * h // 8, w // 8:7 * w // 8]
    return c.mean(), c.std()

out = sys.argv[1]
base = sys.argv[2] if len(sys.argv) > 2 else None
tiles = []
for id in sorted(d for d in os.listdir(out) if os.path.isfile(f'{out}/{d}/shot.png')):
    mean, std = stats(f'{out}/{id}/shot.png')
    flag = 'BLANK' if std < 4 else 'ok'
    print(f'{id} {flag} mean={mean:.0f} std={std:.1f}')
    row = [Image.open(f'{out}/{id}/preview.png') if os.path.exists(f'{out}/{id}/preview.png') else None]
    if base and os.path.exists(f'{base}/{id}/shot.png'):
        row.append(Image.open(f'{base}/{id}/shot.png'))
    row.append(Image.open(f'{out}/{id}/shot.png'))
    tiles.append(row)
if tiles:
    h = 200
    sheet = Image.new('RGB', (3 * 360, h * len(tiles)), '#222')
    for i, row in enumerate(tiles):
        for j, im in enumerate(row):
            if im is None: continue
            im = im.convert('RGB'); im.thumbnail((350, h - 4)); sheet.paste(im, (j * 360, i * h))
    sheet.save(f'{out}/sheet.png')
    print('wrote', f'{out}/sheet.png')
