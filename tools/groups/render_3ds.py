#!/usr/bin/env python3
"""Draw the Groups book page at 320x240.

Rectangles and the English strings come from source/net/groups_logic.h, so a
layout edit shows up here. This is not an emulator capture. Pillow draws the
pixels. The 3DS page itself is citro2d in source/ui/book_pages/page_groups.cpp.
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HEADER = os.path.join(ROOT, 'source', 'net', 'groups_logic.h')
OUT = os.path.join(ROOT, 'layouts', '3ds')
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FONT_B = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

HEADER_C = (0x6E, 0x12, 0x12)
BOTTOM_C = (0x50, 0x0C, 0x0C)
PAPER = (0xFA, 0xF6, 0xEE)
CARD = (0xFF, 0xF8, 0xE4)
EDGE = (0x8C, 0x5E, 0x34)
INK = (0x20, 0x18, 0x10)
SOFT = (0x7A, 0x66, 0x50)
WHITE = (255, 255, 255)
BLUE = (0x4F, 0x8C, 0xE8)
BLUE_D = (0x2B, 0x58, 0xA8)
GREEN = (0x5C, 0xC0, 0x4E)
GREEN_D = (0x2E, 0x7E, 0x26)
RED = (0xE0, 0x3A, 0x2E)
RED_D = (0x9C, 0x1E, 0x16)
YELLOW = (0xFF, 0xD3, 0x3C)
GREY = (0xC8, 0xC2, 0xB6)
GREY_D = (0x8E, 0x86, 0x7A)
GOLD = (0xF5, 0xB8, 0x1A)
TAB = (0x82, 0x1C, 0x1C)
GOLD_TEXT = (0xFF, 0xEE, 0xAA)


def load_header():
    text = open(HEADER, encoding='utf-8').read()
    nums = {m.group(1): float(m.group(2)) for m in re.finditer(r'constexpr float (k\w+) = ([0-9.]+)f', text)}
    strings = {m.group(1): m.group(2) for m in re.finditer(r'const char\* (k\w+) = "([^"]*)"', text)}
    shorts = re.search(r'kKindShort\[3\] = \{"([^"]+)", "([^"]+)", "([^"]+)"\}', text)
    depts = re.search(r'kDepartments\[4\] = \{"([^"]+)", "([^"]+)", "([^"]+)", "([^"]+)"\}', text)
    kinds = re.search(r'kKindNames\[3\] = \{"([^"]+)", "([^"]+)", "([^"]+)"\}', text)
    if not shorts or not depts or not kinds:
        raise SystemExit('groups_logic.h strings did not parse')
    need = ('kFilterX', 'kFilterY', 'kFilterW', 'kFilterH', 'kFilterGap', 'kStatusY', 'kRowX', 'kRowY',
            'kRowW', 'kRowH', 'kRowBtnX', 'kRowBtnY', 'kRowBtnW', 'kRowBtnH', 'kPickCellX', 'kPickCellY',
            'kPickCellW', 'kPickCellH', 'kPickCellGapX', 'kPickCellGapY', 'kPickBackX', 'kPickBackY',
            'kPickBackW', 'kPickBackH', 'kPickTitleY', 'kScreenW', 'kScreenH', 'kTabBarY')
    missing = [n for n in need if n not in nums]
    if missing:
        raise SystemExit('missing layout constants: ' + ', '.join(missing))
    if nums['kFilterH'] < 48 or nums['kRowBtnH'] < 48:
        raise SystemExit('hit targets dropped below 48px')
    strings['kinds'] = kinds.groups()
    strings['shorts'] = shorts.groups()
    strings['depts'] = depts.groups()
    return nums, strings


def font(size, bold=False):
    return ImageFont.truetype(FONT_B if bold else FONT, size)


def fitted_font(draw, text, box_w, size, bold=False):
    f = font(size, bold)
    while size > 7:
        bbox = draw.textbbox((0, 0), text, font=f)
        if bbox[2] - bbox[0] <= box_w:
            return f
        size -= 1
        f = font(size, bold)
    return f


def center_text(draw, box, text, fill, size, bold=False):
    x, y, w, h = box
    f = fitted_font(draw, text, w - 2, size, bold)
    bbox = draw.textbbox((0, 0), text, font=f)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text((x + (w - tw) / 2, y + (h - th) / 2 - 1), text, font=f, fill=fill)


def wrap_text(draw, text, max_w, size):
    f = font(size)
    words = text.split()
    lines, cur = [], ''
    for word in words:
        trial = word if not cur else cur + ' ' + word
        bbox = draw.textbbox((0, 0), trial, font=f)
        if bbox[2] - bbox[0] <= max_w:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def darker(rgb, scale=0.72):
    return tuple(int(c * scale) for c in rgb)


def button(draw, box, face, lip, label, label_fill=WHITE, size=15):
    x, y, w, h = box
    draw.rounded_rectangle((x, y + 3, x + w, y + h), radius=8, fill=lip)
    draw.rounded_rectangle((x, y, x + w, y + h - 3), radius=8, fill=face)
    center_text(draw, (x, y, w, h - 3), label, label_fill, size, bold=True)


def ring(draw, box):
    x, y, w, h = box
    draw.rounded_rectangle((x - 2, y - 2, x + w + 2, y + h + 2), radius=10, outline=(0x10, 0x14, 0x20), width=1)
    draw.rounded_rectangle((x - 1, y - 1, x + w + 1, y + h + 1), radius=9, outline=GOLD, width=2)


def chrome(img, L, title, tabs=True):
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 320, 240), fill=BOTTOM_C)
    d.rectangle((0, 0, 320, 24), fill=HEADER_C)
    paper_h = 214 if not tabs else 188
    d.rectangle((2, 24, 318, 24 + paper_h), fill=PAPER)
    center_text(d, (40, 0, 240, 24), title, WHITE, 13, bold=True)
    d.rounded_rectangle((3, 3, 53, 21), radius=4, fill=TAB)
    center_text(d, (3, 3, 50, 18), '< Prev', GOLD_TEXT, 10, bold=True)
    d.rounded_rectangle((222, 3, 274, 21), radius=4, fill=TAB)
    center_text(d, (222, 3, 52, 18), 'Next >', GOLD_TEXT, 10, bold=True)
    d.rounded_rectangle((282, 3, 316, 21), radius=4, fill=RED)
    center_text(d, (282, 3, 34, 18), 'X', WHITE, 12, bold=True)
    if tabs:
        names = ['Gags', 'Tasks', 'Cogs', 'Suits', 'Fish', 'Trophy', 'SOS', 'Option', 'Map', 'Groups']
        tw = 320 / 10
        for i, name in enumerate(names):
            active = name == 'Groups'
            y = 211 if active else 214
            d.rounded_rectangle((i * tw + 1, y, (i + 1) * tw - 1, 246), radius=4, fill=GOLD if active else TAB)
            center_text(d, (i * tw, y, tw, 26), name, INK if active else WHITE, 8, bold=True)
    return d


def filters(d, L, S, enabled, focus=0):
    for i, name in enumerate(S['shorts']):
        x = L['kFilterX'] + i * (L['kFilterW'] + L['kFilterGap'])
        box = (x, L['kFilterY'], L['kFilterW'], L['kFilterH'])
        button(d, box, BLUE_D if enabled else GREY, darker(BLUE_D) if enabled else GREY_D, name,
               WHITE if enabled else INK, 14)
        if enabled and i == focus:
            ring(d, box)


def row_card(d, L, S, title, place, count, action, mine, enabled, focus=False):
    x, y, w, h = L['kRowX'], L['kRowY'], L['kRowW'], L['kRowH']
    d.rounded_rectangle((x, y, x + w, y + h), radius=8, fill=YELLOW if mine else CARD, outline=EDGE, width=2)
    d.text((16, y + 6), title, font=font(13, True), fill=INK)
    d.text((16, y + 26), place, font=font(11), fill=SOFT)
    bbox = d.textbbox((0, 0), count, font=font(13, True))
    d.text((x + w - 8 - (bbox[2] - bbox[0]), y + 6), count, font=font(13, True), fill=INK)
    if not enabled:
        face, lip, fill = GREY, GREY_D, INK
    elif action == S['kLeave']:
        face, lip, fill = RED_D, darker(RED_D), WHITE
    else:
        face, lip, fill = GREEN_D, darker(GREEN_D), WHITE
    box = (L['kRowBtnX'], L['kRowBtnY'], L['kRowBtnW'], L['kRowBtnH'])
    button(d, box, face, lip, action, fill, 16)
    if focus and enabled:
        ring(d, box)


def picker(d, L, S):
    d.rounded_rectangle((4, 26, 316, 236), radius=8, fill=CARD, outline=EDGE, width=2)
    title = S['kCreateTitle'] + ': ' + S['kinds'][0]
    center_text(d, (10, L['kPickTitleY'] - 4, 300, 18), title, INK, 13, bold=True)
    for i, name in enumerate(S['depts']):
        col, row = i % 2, i // 2
        x = L['kPickCellX'] + col * (L['kPickCellW'] + L['kPickCellGapX'])
        y = L['kPickCellY'] + row * (L['kPickCellH'] + L['kPickCellGapY'])
        box = (x, y, L['kPickCellW'], L['kPickCellH'])
        button(d, box, BLUE_D, darker(BLUE_D), name, WHITE, 13)
        if i == 0:
            ring(d, box)
    box = (L['kPickBackX'], L['kPickBackY'], L['kPickBackW'], L['kPickBackH'])
    button(d, box, RED_D, darker(RED_D), S['kBack'], WHITE, 15)


def status_line(d, L, text):
    if text:
        d.text((10, L['kStatusY']), text, font=font(11), fill=INK)


def render(name, L, S):
    img = Image.new('RGB', (320, 240), PAPER)
    if name == 'picker':
        d = chrome(img, L, 'Groups (10/10)', tabs=False)
        picker(d, L, S)
    else:
        d = chrome(img, L, 'Groups (10/10)', tabs=True)
        if name == 'empty':
            status_line(d, L, '')
            filters(d, L, S, True, 0)
            lines = wrap_text(d, S['kEmpty'], 288, 13)
            d.multiline_text((16, 112), '\n'.join(lines), font=font(13), fill=SOFT, spacing=4)
        elif name == 'loading':
            status_line(d, L, S['kConnecting'])
            filters(d, L, S, True, 0)
        elif name == 'own':
            status_line(d, L, S['kCreated'])
            filters(d, L, S, False)
            row_card(d, L, S, 'Sellbot Boss Battle (' + S['kMine'] + ')', 'Sellbot HQ', '1/8', S['kLeave'],
                     True, True, True)
        elif name == 'leave':
            status_line(d, L, S['kLeft'])
            filters(d, L, S, True, 0)
        elif name == 'error':
            status_line(d, L, 'That group is full.')
            filters(d, L, S, True, 0)
            row_card(d, L, S, 'Cashbot Cog Building', 'Toontown Central', '4/4', S['kFull'], False, False, False)
        else:
            raise SystemExit(name)
    path = os.path.join(OUT, name + '.png')
    img.save(path)
    print(path, img.size)


def main():
    L, S = load_header()
    os.makedirs(OUT, exist_ok=True)
    for name in ('empty', 'loading', 'own', 'leave', 'error', 'picker'):
        render(name, L, S)


if __name__ == '__main__':
    main()
