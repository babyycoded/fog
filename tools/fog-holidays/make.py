#!/usr/bin/env python3
"""Fog holiday sprites (data/fog/holiday/*.png), soft silhouettes in the client's style.
White shapes; the client tints them. Rendered with rsvg-convert:
    python3 tools/fog-holidays/make.py
"""
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(ROOT, '..', '..', 'data', 'fog', 'holiday'))

SOFT = '<defs><filter id="s" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="1.6"/></filter></defs>'

SPRITES = {
    # a bat seen from the front, wings spread; the client squeezes it to flap
    'bat': SOFT + '''<g filter="url(#s)" fill="#fff">
<path d="M128 104 C118 96 112 100 108 108 C96 92 70 84 30 96 C48 104 52 114 50 128 C62 120 74 124 80 136 C88 126 100 128 106 140
C112 132 118 134 128 150 C138 134 144 132 150 140 C156 128 168 126 176 136 C182 124 194 120 206 128 C204 114 208 104 226 96
C186 84 160 92 148 108 C144 100 138 96 128 104 Z"/>
<path d="M118 100 L114 84 L124 96 Z M138 100 L142 84 L132 96 Z"/></g>''',
    'heart': SOFT + '<path filter="url(#s)" fill="#fff" d="M128 210 C70 168 36 136 36 96 C36 64 60 44 86 44 C106 44 120 56 128 72 C136 56 150 44 170 44 C196 44 220 64 220 96 C220 136 186 168 128 210 Z"/>',
    'petal': SOFT + '<path filter="url(#s)" fill="#fff" d="M128 30 C176 70 190 130 160 190 C148 214 108 214 96 190 C66 130 80 70 128 30 Z"/><path d="M128 50 L128 196" stroke="#000" stroke-opacity="0.12" stroke-width="5"/>',
}


def main():
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, body in SPRITES.items():
            path = os.path.join(tmp, name + '.svg')
            with open(path, 'w') as f:
                f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">{body}</svg>')
            subprocess.run(['rsvg-convert', '-w', '128', '-h', '128', '-o', os.path.join(OUT, name + '.png'), path], check=True)
            print('wrote', name)


if __name__ == '__main__':
    main()
