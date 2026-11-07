#!/usr/bin/env python3
"""Fog tee accessories: flat silhouettes with a soft glow, rendered with rsvg-convert.

Every accessory gives two 256x256 PNGs in data/fog/accessories/:
  <id>.png       the silhouette: a flat light shape with fine grain and a faint rim light.
                 The client tints it with the colour you pick, so it can be a dark shadow
                 or a pale ghost.
  <id>_glow.png  a blurred white halo of the shape plus its light sources (flame, ring,
                 crack...). The client adds it with its own glow colour, so the piece seems
                 to come out of the fog.
No outlines, no gloss. The anchor (attach point) is the centre of the canvas.
    python3 tools/fog-accessories/make.py
"""
import math
import os
import random
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(ROOT, '..', '..', 'data', 'fog', 'accessories'))

BODY_DEFS = '''
<defs>
  <linearGradient id="body" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#f4f6f9"/><stop offset="1" stop-color="#c3c8d0"/>
  </linearGradient>
  <filter id="grain" x="-10%" y="-10%" width="120%" height="120%">
    <feTurbulence type="fractalNoise" baseFrequency="0.9" numOctaves="2" seed="7" result="noise"/>
    <feColorMatrix in="noise" type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 0.16 0" result="dots"/>
    <feComposite in="dots" in2="SourceGraphic" operator="in" result="grainy"/>
    <feMerge><feMergeNode in="SourceGraphic"/><feMergeNode in="grainy"/></feMerge>
  </filter>
  <filter id="rough" x="-10%" y="-10%" width="120%" height="120%">
    <feTurbulence type="fractalNoise" baseFrequency="0.035" numOctaves="2" seed="3" result="wobble"/>
    <feDisplacementMap in="SourceGraphic" in2="wobble" scale="7" xChannelSelector="R" yChannelSelector="G" result="wobbled"/>
    <feTurbulence type="turbulence" baseFrequency="0.22" numOctaves="1" seed="9" result="tear"/>
    <feDisplacementMap in="wobbled" in2="tear" scale="4" xChannelSelector="R" yChannelSelector="G"/>
  </filter>
  <pattern id="hatch" width="7" height="7" patternUnits="userSpaceOnUse" patternTransform="rotate(-35)">
    <path d="M0 0 L0 7" stroke="#2c3036" stroke-width="1.4" stroke-opacity="0.9"/>
  </pattern>
  <filter id="rim" x="-10%" y="-10%" width="120%" height="120%">
    <feOffset dx="0" dy="3" in="SourceAlpha" result="down"/>
    <feComposite in="SourceAlpha" in2="down" operator="out" result="edge"/>
    <feFlood flood-color="#ffffff" flood-opacity="0.9"/>
    <feComposite in2="edge" operator="in"/>
  </filter>
</defs>'''

GLOW_DEFS = '''
<defs>
  <filter id="haze" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="9"/></filter>
  <filter id="soft" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="4"/></filter>
</defs>'''

R = random.Random(11)



def poly(pts):
    return 'M' + ' L'.join(f'{x:.1f} {y:.1f}' for x, y in pts) + ' Z'


def spiky(fn, n, amp, seed, inward=0.35):
    """a closed outline fn(t) (t in 0..1) with hand-drawn thorns: every other point is pushed
    out along the normal by a random amount, the rest pulled in a little"""
    rnd = random.Random(seed)
    pts = []
    for k in range(n):
        t = k / n
        x0, y0 = fn(t)
        x1, y1 = fn((t + 0.001) % 1.0)
        tx, ty = x1 - x0, y1 - y0
        ln = math.hypot(tx, ty) or 1.0
        nx, ny = ty / ln, -tx / ln
        if k % 2 == 0:
            d = amp * (0.35 + rnd.random() * 0.65) * (1.6 if rnd.random() < 0.18 else 1.0)
        else:
            d = -amp * inward * rnd.random()
        pts.append((x0 + nx * d, y0 + ny * d))
    return poly(pts)


def flame(t):
    a = t * math.tau
    r = 1.0 + 0.9 * max(0.0, -math.sin(a)) ** 3
    return 128 + math.cos(a) * 46 * (1.0 - 0.45 * max(0.0, -math.sin(a))), 170 + math.sin(a) * 52 * r


def heart(t):
    a = t * math.tau
    x = 16 * math.sin(a) ** 3
    y = -(13 * math.cos(a) - 5 * math.cos(2 * a) - 2 * math.cos(3 * a) - math.cos(4 * a))
    return 128 + x * 5.6, 124 + y * 5.6


def cloud(t):
    a = t * math.tau
    bump = abs(math.sin(a * 3.5)) * 0.35 + abs(math.sin(a * 6 + 1)) * 0.12
    r = 1.0 + bump * (1.0 if math.sin(a) < 0.2 else 0.25)
    return 128 + math.cos(a) * 92 * r * 0.95, 140 + math.sin(a) * 44 * r


def moon_outer(t):
    a = math.pi * 0.5 + t * math.pi
    return 150 + math.cos(a) * 96, 128 - math.sin(a) * 94


def moth_wing(side, lower):
    def fn(t):
        a = t * math.tau
        if lower:
            r = 44 + 18 * math.cos(a * 2) ** 2
            return 128 + side * (36 + math.cos(a) * r * 0.7), 158 + math.sin(a) * r * 0.6
        r = 58 + 22 * math.cos(a * 2) ** 2
        return 128 + side * (52 + math.cos(a) * r * 0.8), 104 + math.sin(a) * r * 0.66 - side * 0 - (math.cos(a) * 14)
    return fn


def halo_shards():
    out, rnd = [], random.Random(5)
    k = 0
    a = 0.3
    while a < math.tau - 0.6:
        span = 0.22 + rnd.random() * 0.3
        pts = []
        for j in range(7):
            b = a + span * j / 6
            pts.append((128 + 90 * math.cos(b), 128 + 26 * math.sin(b) - 5 - (4 if j % 2 else 0)))
        for j in range(6, -1, -1):
            b = a + span * j / 6
            pts.append((128 + 80 * math.cos(b), 128 + 20 * math.sin(b) + 4 + (3 if j % 3 == 0 else 0)))
        out.append(poly(pts))
        a += span + 0.05 + rnd.random() * 0.07
        k += 1
    return ' '.join(out)


def torn_hem(x0, x1, y, n, amp, seed):
    rnd = random.Random(seed)
    pts = []
    for k in range(n + 1):
        x = x0 + (x1 - x0) * k / n
        pts.append((x, y + (amp * (0.4 + rnd.random()) if k % 2 else -amp * 0.3 * rnd.random())))
    return pts


def feather(x, y, angle, length, width):
    """a soft feather: wide in the middle, a rounded slightly hooked tip"""
    a = math.radians(angle)
    dx, dy = math.cos(a), math.sin(a)
    nx, ny = -dy, dx
    left, right = [], []
    for k in range(13):
        t = k / 12
        w = width * math.sin(math.pi * min(t ** 0.75, 1.0)) * (1.0 - 0.15 * t)
        bend = 0.08 * length * t * t
        cx, cy = x + dx * length * t + nx * bend, y + dy * length * t + ny * bend
        left.append((cx + nx * w, cy + ny * w))
        right.append((cx - nx * w * 0.7, cy - ny * w * 0.7))
    pts = left + right[::-1]
    return 'M' + ' L'.join(f'{px:.1f} {py:.1f}' for px, py in pts) + ' Z'


def needle(x, y, angle, length, width):
    """a thin pointed feather, like a needle"""
    a = math.radians(angle)
    dx, dy = math.cos(a), math.sin(a)
    nx, ny = -dy, dx
    tx, ty = x + dx * length, y + dy * length
    c1 = (x + dx * length * 0.35 + nx * width, y + dy * length * 0.35 + ny * width)
    c2 = (x + dx * length * 0.35 - nx * width, y + dy * length * 0.35 - ny * width)
    return f'M{x:.1f} {y:.1f} Q{c1[0]:.1f} {c1[1]:.1f} {tx:.1f} {ty:.1f} Q{c2[0]:.1f} {c2[1]:.1f} {x:.1f} {y:.1f} Z'


def needle_wing(mirror):
    """the wings the user picked: needle feathers fanning up and out from the shoulder"""
    rnd = random.Random(11)
    for _ in range(0 if not mirror else 25):
        rnd.random()
    paths = []
    root = (140, 132)
    rows = [(10, 118, 8, -62, 28), (8, 82, 9, -50, 24), (6, 50, 10, -36, 20)]
    for row, (count, length, width, start, end) in enumerate(rows):
        for i in range(count):
            ang = start + i * (end - start) / (count - 1)
            ln = length * (0.82 + 0.18 * math.sin(i * 1.3 + row)) * (0.72 if rnd.random() < 0.2 else 1.0)
            x = root[0] + row * 2
            if mirror:
                ang, x = 180 - ang, 256 - x
            paths.append(needle(x, root[1], ang, ln, width))
    return ' '.join(paths)


def wing(mirror):
    """drooping wings: layered feathers hanging down and out from the shoulder"""
    paths = []
    rows = [(9, 124, 15, -38, 64, (142, 120)), (7, 84, 16, -42, 52, (144, 114)), (6, 52, 15, -48, 36, (146, 108))]
    for row, (count, length, width, start, end, root) in enumerate(rows):
        for i in range(count):
            ang = start + i * (end - start) / (count - 1)
            ln = length * (0.86 + 0.14 * math.sin(i * 1.3 + row)) * (0.74 if R.random() < 0.16 else 1.0)
            x, y = root[0] + i * 1.5, root[1] + i * 1.2
            if mirror:
                ang, x = 180 - ang, 256 - x
            paths.append(feather(x, y, ang, ln, width))
    return ' Z '.join(p.rstrip(' Z') for p in paths) + ' Z'


def thorns():
    """two twisted branches around an ellipse with thorns"""
    out = []
    for strand in range(2):
        pts = []
        for k in range(0, 73):
            t = k / 72 * math.tau
            r = 6 * math.sin(t * 6 + strand * math.pi)
            pts.append((128 + (92 + r) * math.cos(t), 128 + (30 + r * 0.6) * math.sin(t)))
        out.append('<path d="M' + ' L'.join(f'{x:.1f} {y:.1f}' for x, y in pts) + 'Z" fill="none" stroke="url(#body)" stroke-width="6" stroke-linejoin="round"/>')
    spikes = []
    for k in range(14):
        t = k / 14 * math.tau + 0.2
        x, y = 128 + 94 * math.cos(t), 128 + 31 * math.sin(t)
        ang = t + (0.9 if k % 2 else -0.9)
        ox, oy = math.cos(ang) * 16, math.sin(ang) * 10 - 6
        spikes.append(f'M{x - 3:.1f} {y:.1f} L{x + ox:.1f} {y + oy:.1f} L{x + 3:.1f} {y:.1f} Z')
    out.append(f'<path d="{" ".join(spikes)}" fill="url(#body)"/>')
    return '\n'.join(out)


SHAPES = {
    'broken_halo': (f'<path d="{halo_shards()}" fill="url(#body)"/>', f'<path d="{halo_shards()}" fill="#fff"/>'),
    'fallen_wings': (f'<path d="{needle_wing(False)}" fill="url(#body)" fill-opacity="0.96"/><path d="{needle_wing(True)}" fill="url(#body)" fill-opacity="0.96"/>', ''),
    'thorn_crown': (thorns(), ''),
    # a thin cigarette; the client adds the ember and smoke at its right end
    'cigarette': ('''
<rect x="66" y="121" width="134" height="14" rx="4" fill="url(#body)"/>
<rect x="66" y="121" width="34" height="14" rx="4" fill="#9ea4ad"/>
<rect x="194" y="121" width="10" height="14" rx="3" fill="#5c626b"/>''', ''),
    # faint streaks under the eyes; the client lets drops fall from their ends
    'tears': ('''
<path d="M80 86 Q74 140 80 196 Q84 200 88 196 Q92 140 86 86 Z" fill="url(#body)" fill-opacity="0.85"/>
<path d="M170 86 Q164 140 170 196 Q174 200 178 196 Q182 140 176 86 Z" fill="url(#body)" fill-opacity="0.85"/>''',
              '<path d="M83 90 L82 194 M173 90 L172 194" stroke="#fff" stroke-width="5" stroke-linecap="round"/>'),
    'rain_cloud': (f'<path d="{spiky(cloud, 110, 7, 9)}" fill="url(#body)"/>', ''),
    'umbrella': ('<path d="M127 136 L127 232 Q127 248 113 247 Q102 246 102 234" fill="none" stroke="url(#body)" stroke-width="5" stroke-linecap="round"/>'
                 + f'<path d="M16 138 Q26 44 128 38 Q230 44 240 138 L{poly(torn_hem(240, 16, 138, 22, 9, 6))[1:-2]} Z" fill="url(#body)" fill-opacity="0.72"/>'
                 + '<path d="M128 40 L128 132 M128 40 Q72 62 58 136 M128 40 Q184 62 198 136" fill="none" stroke="#2c3036" stroke-opacity="0.25" stroke-width="3"/>', ''),
    'moth': (''.join(f'<path d="{spiky(moth_wing(sd, lw), 70, 6, 20 + i)}" fill="url(#body)"/>' for i, (sd, lw) in enumerate([(-1, False), (1, False), (-1, True), (1, True)]))
             + '<ellipse cx="128" cy="134" rx="8" ry="38" fill="url(#body)"/><path d="M124 98 Q110 66 90 58 M132 98 Q146 66 166 58" fill="none" stroke="url(#body)" stroke-width="3" stroke-linecap="round"/>'
             + '<circle cx="78" cy="98" r="9" fill="#2c3036" fill-opacity="0.45"/><circle cx="178" cy="98" r="9" fill="#2c3036" fill-opacity="0.45"/>',
             '<path d="M68 70 Q40 90 52 120 M188 70 Q216 90 204 120" stroke="#fff" stroke-width="8" fill="none" stroke-opacity="0.6"/>'),
    'wilted_flower': ('''
<path d="M112 250 Q106 160 132 100 Q150 64 186 72" fill="none" stroke="url(#body)" stroke-width="5" stroke-linecap="round"/>
<path d="M114 196 Q80 180 74 152 Q102 156 116 182 Z" fill="url(#body)"/>
<g transform="translate(194 104) rotate(155)" fill="url(#body)">
<path d="M0 0 Q-22 -26 -8 -56 Q0 -40 8 -56 Q22 -26 0 0 Z"/><path d="M0 0 Q-22 -26 -8 -56 Q0 -40 8 -56 Q22 -26 0 0 Z" transform="rotate(-48)"/>
<path d="M0 0 Q-22 -26 -8 -56 Q0 -40 8 -56 Q22 -26 0 0 Z" transform="rotate(48)"/><circle r="10"/></g>
<path d="M214 196 q10 6 4 22 q-12 -6 -4 -22 Z" fill="url(#body)" fill-opacity="0.8"/>''', ''),
    'plasters': ('''
<g fill="url(#body)" fill-rule="evenodd">
<path transform="rotate(35 128 128)" d="M60 104 h136 a24 24 0 0 1 0 48 h-136 a24 24 0 0 1 0 -48 Z M114 120 a4 4 0 1 0 0.1 0 Z M128 120 a4 4 0 1 0 0.1 0 Z M142 120 a4 4 0 1 0 0.1 0 Z M114 136 a4 4 0 1 0 0.1 0 Z M128 136 a4 4 0 1 0 0.1 0 Z M142 136 a4 4 0 1 0 0.1 0 Z"/>
<path transform="rotate(-35 128 128)" d="M70 108 h116 a20 20 0 0 1 0 40 h-116 a20 20 0 0 1 0 -40 Z" fill-opacity="0.9"/></g>''', ''),
    'headphones': ('''
<path d="M44 160 Q38 54 128 50 Q218 54 212 160" fill="none" stroke="url(#body)" stroke-width="9" stroke-linecap="round"/>
<path d="M58 96 L44 84 L66 88 Z M80 70 L72 52 L90 64 Z M110 56 L108 36 L122 54 Z M146 54 L148 34 L134 54 Z M176 64 L184 46 L166 58 Z M198 88 L212 76 L190 82 Z" fill="url(#body)"/>
<rect x="24" y="132" width="40" height="76" rx="18" fill="url(#body)"/>
<rect x="192" y="132" width="40" height="76" rx="18" fill="url(#body)"/>
<rect x="58" y="146" width="10" height="48" rx="5" fill="#8a909a"/><rect x="188" y="146" width="10" height="48" rx="5" fill="#8a909a"/>
<path d="M212 206 Q222 244 182 248 Q150 250 140 230" fill="none" stroke="url(#body)" stroke-width="3" stroke-linecap="round"/>''', ''),
    'crescent_moon': (f'<path fill="url(#body)" d="{spiky(lambda t: moon_outer(t), 60, 6, 4)[:-2]} A76 76 0 1 1 150 34 Z"/>',
                      '<path fill="#fff" d="M150 34 A96 96 0 1 0 150 222 A76 76 0 1 1 150 34 Z"/>'),
    'wisp': (f'<path fill="url(#body)" fill-opacity="0.8" d="{spiky(flame, 46, -12, 8, 0.2)}"/>',
             '<path fill="#fff" d="M128 70 Q146 110 160 128 Q176 158 158 186 Q144 204 128 204 Q112 204 98 186 Q80 158 96 128 Q110 110 128 70 Z"/><circle cx="128" cy="170" r="22" fill="#fff"/>'),
    'lantern': ('''
<path d="M128 6 L128 50" stroke="url(#body)" stroke-width="3"/>
<rect x="104" y="44" width="48" height="14" rx="4" fill="#7c828c"/>
<path d="M104 58 Q50 70 52 140 Q54 206 104 220 L152 220 Q202 206 204 140 Q206 70 152 58 Z" fill="url(#body)" fill-opacity="0.92"/>
<path d="M80 72 Q66 140 80 208 M104 62 Q96 140 104 218 M152 62 Q160 140 152 218 M176 72 Q190 140 176 208" fill="none" stroke="#2c3036" stroke-opacity="0.2" stroke-width="3"/>
<rect x="104" y="218" width="48" height="12" rx="4" fill="#7c828c"/>
<path d="M120 230 L118 252 M128 230 L128 254 M136 230 L138 252" stroke="url(#body)" stroke-width="2"/>''',
                '<ellipse cx="128" cy="140" rx="62" ry="70" fill="#fff"/>'),
    'veil': (f'<path d="M40 70 Q128 20 216 70 Q236 150 222 228 L{poly(torn_hem(222, 34, 228, 26, 12, 13))[1:-2]} Q20 150 40 70 Z" fill="url(#body)" fill-opacity="0.42"/>'
             + '<path d="M40 70 Q128 20 216 70" fill="none" stroke="url(#body)" stroke-width="5"/>'
             + '<g fill="url(#body)" fill-opacity="0.5"><circle cx="70" cy="120" r="3"/><circle cx="100" cy="150" r="3"/><circle cx="130" cy="112" r="3"/><circle cx="160" cy="150" r="3"/><circle cx="190" cy="118" r="3"/><circle cx="84" cy="190" r="3"/><circle cx="128" cy="184" r="3"/><circle cx="172" cy="192" r="3"/></g>', ''),
    'broken_heart': (f'<path d="{spiky(heart, 90, 9, 3)}" fill="url(#body)"/><path d="M130 72 L120 100 L138 122 L116 150 L134 176 L126 206" fill="none" stroke="#2c3036" stroke-width="7" stroke-linejoin="miter"/>',
                     '<path d="M130 72 L120 100 L138 122 L116 150 L134 176 L126 206" fill="none" stroke="#fff" stroke-width="9" stroke-linejoin="miter"/>'),
}

# particles used by the animations (not in the catalogue)
FX = {
    'fx_ring': '<defs><filter id="b" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="5"/></filter></defs><circle cx="128" cy="128" r="104" fill="none" stroke="#fff" stroke-width="9" filter="url(#b)"/><circle cx="128" cy="128" r="104" fill="none" stroke="#fff" stroke-width="2.5"/>',
    'fx_puff': '<defs><radialGradient id="g" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#fff"/><stop offset="0.5" stop-color="#fff" stop-opacity="0.3"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient></defs><circle cx="128" cy="128" r="124" fill="url(#g)"/>',
    'fx_drop': '<defs><linearGradient id="d" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity="0.5"/><stop offset="1" stop-color="#fff"/></linearGradient></defs><path d="M128 24 Q184 120 178 168 Q172 230 128 232 Q84 230 78 168 Q72 120 128 24 Z" fill="url(#d)"/>',
}


# drawn clean, without the rough hand-drawn pass (the user liked them that way)
CLEAN = {'fallen_wings'}


def render(svg_body, name, tmp):
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">{svg_body}</svg>'
    path = os.path.join(tmp, name + '.svg')
    with open(path, 'w') as f:
        f.write(svg)
    subprocess.run(['rsvg-convert', '-w', '256', '-h', '256', '-o', os.path.join(OUT, name + '.png'), path], check=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, (shape, light) in SHAPES.items():
            hatch = shape.replace('url(#body)', 'url(#hatch)')
            if name in CLEAN:
                body = f'{BODY_DEFS}<g filter="url(#grain)">{shape}</g><g filter="url(#rim)" opacity="0.45">{shape}</g>'
            else:
                body = (f'{BODY_DEFS}<g filter="url(#rough)"><g filter="url(#grain)">{shape}</g>'
                        f'<g opacity="0.16" transform="translate(0 2)">{hatch}</g>'
                        f'<g filter="url(#rim)" opacity="0.45">{shape}</g></g>')
            render(body, name, tmp)
            white = shape.replace('url(#body)', '#ffffff').replace('#9ea4ad', '#ffffff').replace('#5c626b', '#ffffff').replace('#8a909a', '#ffffff').replace('#7c828c', '#ffffff')
            glow = f'{GLOW_DEFS}<g filter="url(#haze)" opacity="0.55">{white.replace("url(#hatch)", "#ffffff")}</g>'
            if light:
                glow += f'<g filter="url(#haze)">{light}</g><g filter="url(#soft)" opacity="0.8">{light}</g>'
            render(glow, name + '_glow', tmp)
            print('wrote', name)
        for name, svg in FX.items():
            render(svg, name, tmp)


if __name__ == '__main__':
    main()
