"""Groups page sizes. Pure Python, no Panda.

aspect2d is 2 units tall, so one unit is win_height/2 pixels before the book
contain-fit a fold applies (the same fit as the half-fold book layout). Join,
Full and Leave are sized so that after that fit they are still at least 48dp
and 44pt. 48 is the stricter of the two at one scale factor, so a target that
meets 48dp also meets 44pt.

A short window (the 320x240 class) uses a compact stack: one large row, bigger
type, and a Join, Full or Leave button that spans the row.
"""
from . import strings as S

MIN_DP = 48
MIN_PT = 44
REF_W = 8.0 / 3.0
REF_H = 2.0


def book_fit(width, height):
    """Contain-fit of the 4:3 book into this window. 1.0 on 4:3 and wider."""
    aspect = width / float(height) if height else 1.0
    if aspect >= 1.0:
        bw, bh = 2.0 * aspect, 2.0
    else:
        bw, bh = 2.0, 2.0 / aspect
    return min(bw / REF_W, bh / REF_H)


def is_compact(width, height):
    return height <= 320 and width <= 480


# Visible list height of the taller standalone sheet (frame z from 0 down to -0.78).
TALL_LIST = 0.78


def list_frame(m):
    """Top and bottom of the scrolled list, in page units.

    The list fills the room under the filters, down to the Leave button. On a
    normal window that room is at least the taller 0.78 sheet. A compact window
    keeps a single row.
    """
    z = 0.86 if m.compact else 0.66
    title_s = 0.16 if m.compact else 0.11
    z -= title_s * 0.9 + 0.03
    st_s = 0.08 if m.compact else 0.05
    z -= st_s + 0.04
    list_top = z - m.touch - 0.04
    floor = -0.96 if m.compact else (-1.0 + m.touch + 0.08)
    room = max(m.row_h, list_top - floor)
    shown = 1 if m.compact else max(1, int(room / m.row_h))
    return list_top, list_top - shown * m.row_h


def window_half(width, height):
    """Half-extents of aspect2d for this window, in aspect2d units."""
    aspect = width / float(height) if height else 1.0
    if aspect >= 1.0:
        return aspect, 1.0
    return 1.0, 1.0 / aspect


def picker_back_z(m):
    """Center z of the picker Back button. The whole frame stays inside the window."""
    limit = -1.0 + m.button_h / 2.0 + 0.02
    preferred = -0.82 if m.compact else -0.55
    return max(preferred, limit)


def list_message(ready, group_count, status):
    """Status line and whether the empty-list line is shown.

    Connecting and the empty line are never both shown. A result or error
    replaces the empty line until the next quiet refresh.
    """
    if not ready:
        return S.Connecting, False
    if status == S.Connecting:
        status = ''
    return status, group_count == 0 and not status


class PageLayout(object):
    def __init__(self, **kw):
        self.__dict__.update(kw)

    def on_screen(self, units):
        """Pixels after the book contain-fit."""
        return units * self.px_per_unit * self.fit


def page_layout(width, height, density=1.0):
    width = max(int(width), 1)
    height = max(int(height), 1)
    density = max(float(density), 1.0)
    fit = book_fit(width, height)
    ppu = height / 2.0
    min_px = max(MIN_DP, MIN_PT) * density
    touch = min_px / (ppu * fit)
    compact = is_compact(width, height)
    if compact:
        title_scale = max(0.13, 15.0 / ppu)
        place_scale = max(0.11, 13.0 / ppu)
        filter_scale = max(0.105, 12.0 / ppu)
        button_h = touch
        button_w = 1.40
        # Two title lines, the place line, then the full-width button.
        row_h = 0.05 + title_scale * 2.3 + place_scale * 1.4 + 0.06 + button_h
        rows_shown = 1
        full_width = True
        filter_labels = S.KindShort
    else:
        title_scale = 0.055
        place_scale = 0.048
        filter_scale = min(0.05, touch * 0.34)
        button_h = touch
        button_w = max(touch * 1.6, 0.36)
        row_h = max(0.18, button_h + title_scale + place_scale + 0.04)
        rows_shown = max(1, int(0.70 / row_h))
        full_width = False
        # Two-line names only fit a tall button. A 48px button uses one word.
        filter_labels = S.KindButtons if touch >= 0.20 else S.KindShort
    lay = PageLayout(
        width=width, height=height, density=density, fit=fit, px_per_unit=ppu,
        compact=compact, touch=touch, touch_px=min_px,
        title_scale=title_scale, place_scale=place_scale, filter_scale=filter_scale,
        button_w=button_w, button_h=button_h, row_h=row_h, rows_shown=rows_shown,
        full_width=full_width, filter_labels=filter_labels,
    )
    half_w, half_h = window_half(width, height)
    pad = 0.02
    lay.cover = (-half_w - pad, half_w + pad, -half_h - pad, half_h + pad)
    lay.button_px_w = lay.on_screen(button_w)
    lay.button_px_h = lay.on_screen(button_h)
    lay.title_px = lay.on_screen(title_scale)
    lay.place_px = lay.on_screen(place_scale)
    lay.filter_px = lay.on_screen(filter_scale)
    return lay
