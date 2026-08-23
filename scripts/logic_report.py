#!/usr/bin/env python3
"""One-time: what every program and every function block does inside.

Not wired into the build. It draws each POU and each block once, into a PDF to
read and an editable .drawio.svg beside it, from ONE model - so the page and the
diagram a reader opens in draw.io cannot disagree.

    python3 scripts/logic_report.py

A program's diagram is DERIVED from its table, so it cannot rot. A block's is
authored below from its source, because what a Pid does inside is not in any
table.
"""

import html
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vars_programs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, "src", "sites", "gh_mlb41")
IMAGES = os.path.join(ROOT, "images", "logic")

# The one palette, so the PDF and the SVG cannot drift apart. A row is coloured
# by WHAT IT IS - the scope its table declares - not by which section of the POU
# names it, because that is the fact a reader is missing at the cabinet.
INK    = "#1a1a1a"
MUTED  = "#6b6b6b"
FIELD_IN  = "#d6e8f7"   # MI / AI / DI - the field, read
FIELD_OUT = "#d7efdb"   # RO / AO / DO / MO - the field, driven
STATUS_C  = "#dfe4ea"   # SV / SVE / SVR - published
PARAM_C   = "#fdf1d0"   # P / PS / PE - an operator's number
ALARM_C   = "#f7d5d1"   # A
GLOBAL_C  = "#e6ddf2"   # G / GR
BLK_C     = "#f0dfbe"   # a function block it holds
FN_C      = "#dcecdd"   # a FUNCTION it calls - stateless, held by nobody
VAR_C     = "#efeef6"   # what it remembers - not a row at all
PLAIN     = "#ffffff"
FALL_C    = "#2f6fae"   # the falling branch of a hysteretic curve

KIND_FILL = {"MI": FIELD_IN, "AI": FIELD_IN, "DI": FIELD_IN,
             "RO": FIELD_OUT, "AO": FIELD_OUT, "DO": FIELD_OUT,
             "MO": FIELD_OUT,
             "SV": STATUS_C, "SVE": STATUS_C, "SVR": STATUS_C,
             "P": PARAM_C, "PS": PARAM_C, "PE": PARAM_C,
             "A": ALARM_C, "G": GLOBAL_C, "GR": GLOBAL_C}

KIND_NAME = {FIELD_IN: "field", FIELD_OUT: "field, driven",
             STATUS_C: "status", PARAM_C: "parameter", ALARM_C: "alarm",
             GLOBAL_C: "global"}

# A value that survives a power cut: a retained scope, or a parameter the table
# marks. Drawn as a dot rather than a seventh colour.
RETAINED_KINDS = ("SVR", "GR")

# On a BLOCK's page there are no rows, only what goes in and what comes out.
IN_C, OUT_C = FIELD_IN, FIELD_OUT


class Prim:
    """A drawn thing: the whole vocabulary both renderers understand."""

    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)


def rect(x, y, w, h, label="", fill=PLAIN, sub="", dashed=False, dot=False):
    return Prim("rect", x=x, y=y, w=w, h=h, label=label, fill=fill, sub=sub,
                dashed=dashed, dot=dot)


def dot(x, y, r=2.2):
    return Prim("dot", x=x, y=y, r=r)


def text(x, y, s, size=8, anchor="start", color=INK, bold=False):
    return Prim("text", x=x, y=y, s=s, size=size, anchor=anchor, color=color,
                bold=bold)


def line(pts, arrow=False, label="", dashed=False, color=MUTED):
    return Prim("line", pts=pts, arrow=arrow, label=label, dashed=dashed,
                color=color)


# --- the two renderers -------------------------------------------------------

def to_pdf(c, prims, ox, oy, scale=1.0):
    """Draw @p prims onto a reportlab canvas. Model y grows DOWN; the page's
    grows up, so oy is the model's origin measured from the page bottom."""
    def X(x):
        return ox + x * scale

    def Y(y):
        return oy - y * scale

    for p in prims:
        if p.kind == "rect":
            c.setFillColor(p.fill)
            c.setStrokeColor(INK if not p.dashed else MUTED)
            c.setLineWidth(0.6)
            c.setDash([2, 2] if p.dashed else [])
            c.roundRect(X(p.x), Y(p.y + p.h), p.w * scale, p.h * scale,
                        2.5 * scale, stroke=1, fill=1)
            c.setDash([])
            if p.label:
                c.setFillColor(INK)
                c.setFont("Helvetica", 7.6 * scale)
                c.drawCentredString(X(p.x + p.w / 2.0),
                                    Y(p.y + (9.5 if p.sub else p.h / 2.0 + 2.6)),
                                    p.label)
            if p.sub:
                c.setFillColor(MUTED)
                c.setFont("Helvetica", 6.2 * scale)
                c.drawCentredString(X(p.x + p.w / 2.0), Y(p.y + p.h - 4.5),
                                    p.sub)
        elif p.kind == "dot":
            c.setFillColor(INK)
            c.setStrokeColor(INK)
            c.circle(X(p.x), Y(p.y), p.r * scale, stroke=0, fill=1)
        elif p.kind == "text":
            c.setFillColor(p.color)
            c.setFont("Helvetica-Bold" if p.bold else "Helvetica", p.size * scale)
            fn = {"start": c.drawString, "middle": c.drawCentredString,
                  "end": c.drawRightString}[p.anchor]
            fn(X(p.x), Y(p.y), p.s)
        elif p.kind == "line":
            c.setStrokeColor(p.color)
            c.setLineWidth(0.7)
            c.setDash([2, 2] if p.dashed else [])
            path = c.beginPath()
            path.moveTo(X(p.pts[0][0]), Y(p.pts[0][1]))
            for x, y in p.pts[1:]:
                path.lineTo(X(x), Y(y))
            c.drawPath(path)
            c.setDash([])
            if p.arrow:
                _arrow_pdf(c, X(p.pts[-2][0]), Y(p.pts[-2][1]),
                           X(p.pts[-1][0]), Y(p.pts[-1][1]), p.color, scale)
            if p.label:
                mx, my, anc = _label_at(p.pts)
                c.setFillColor(p.color)
                c.setFont("Helvetica", 6.2 * scale)
                draw = c.drawCentredString if anc == "middle" else c.drawString
                draw(X(mx), Y(my), p.label)


def _label_at(pts):
    """Beside the longest RUN of a route, not between its two ends: an elbow's
    midpoint sits wherever the elbow went, which is often inside a box."""
    best, run = (pts[0], pts[-1]), -1.0
    for a, b in zip(pts, pts[1:]):
        d = abs(b[0] - a[0]) + abs(b[1] - a[1])
        if d > run:
            best, run = (a, b), d
    (ax, ay), (bx, by) = best
    mx, my = (ax + bx) / 2.0, (ay + by) / 2.0
    if abs(bx - ax) < abs(by - ay):
        return mx + 4, my, "start"
    return mx, my - 3, "middle"


def _arrow_pdf(c, x0, y0, x1, y1, color, scale):
    import math
    a = math.atan2(y1 - y0, x1 - x0)
    h = 4.0 * scale
    c.setFillColor(color)
    path = c.beginPath()
    path.moveTo(x1, y1)
    path.lineTo(x1 - h * math.cos(a - 0.4), y1 - h * math.sin(a - 0.4))
    path.lineTo(x1 - h * math.cos(a + 0.4), y1 - h * math.sin(a + 0.4))
    path.close()
    c.drawPath(path, stroke=0, fill=1)


def to_svg(prims, w, h, title):
    """The same model as an editable .drawio.svg: the shapes to look at, and the
    mxfile the editor reopens, in one file."""
    body, cells, cid = [], [], 2
    for p in prims:
        if p.kind == "rect":
            dash = ' stroke-dasharray="3,3"' if p.dashed else ""
            body.append(f'<rect x="{p.x}" y="{p.y}" width="{p.w}" height="{p.h}" '
                        f'rx="3" fill="{p.fill}" stroke="{INK}" '
                        f'stroke-width="0.6"{dash}/>')
            if p.label:
                body.append(_svg_text(p.x + p.w / 2.0,
                                      p.y + (9.5 if p.sub else p.h / 2.0 + 2.6),
                                      p.label, 7.6, "middle", INK))
            if p.sub:
                body.append(_svg_text(p.x + p.w / 2.0, p.y + p.h - 4.5, p.sub,
                                      6.2, "middle", MUTED))
            style = (f"rounded=1;fillColor={p.fill};strokeColor={INK};"
                     f"whiteSpace=wrap;html=1;fontSize=8"
                     + (";dashed=1" if p.dashed else ""))
            cells.append(f'<mxCell id="n{cid}" value="{_x(p.label)}" '
                         f'style="{style}" vertex="1" parent="1">'
                         f'<mxGeometry x="{p.x}" y="{p.y}" width="{p.w}" '
                         f'height="{p.h}" as="geometry"/></mxCell>')
            cid += 1
        elif p.kind == "dot":
            body.append(f'<circle cx="{p.x}" cy="{p.y}" r="{p.r}" '
                        f'fill="{INK}"/>')
            cells.append(f'<mxCell id="d{cid}" value="" '
                         f'style="ellipse;fillColor={INK};strokeColor=none" '
                         f'vertex="1" parent="1"><mxGeometry x="{p.x - p.r}" '
                         f'y="{p.y - p.r}" width="{p.r * 2}" '
                         f'height="{p.r * 2}" as="geometry"/></mxCell>')
            cid += 1
        elif p.kind == "text":
            body.append(_svg_text(p.x, p.y, p.s, p.size, p.anchor, p.color,
                                  p.bold))
            style = (f"text;html=1;fontSize={int(p.size)};fontColor={p.color};"
                     f"align={'center' if p.anchor == 'middle' else 'left'}"
                     + (";fontStyle=1" if p.bold else ""))
            cells.append(f'<mxCell id="n{cid}" value="{_x(p.s)}" style="{style}" '
                         f'vertex="1" parent="1"><mxGeometry x="{p.x}" '
                         f'y="{p.y - p.size}" width="{max(40, len(p.s) * 5)}" '
                         f'height="{p.size + 4}" as="geometry"/></mxCell>')
            cid += 1
        elif p.kind == "line":
            pts = " ".join(f"{x},{y}" for x, y in p.pts)
            dash = ' stroke-dasharray="3,3"' if p.dashed else ""
            body.append(f'<polyline points="{pts}" fill="none" '
                        f'stroke="{p.color}" stroke-width="0.7"{dash}'
                        + (f' marker-end="url(#a)"' if p.arrow else "") + '/>')
            if p.label:
                mx, my, anc = _label_at(p.pts)
                body.append(_svg_text(mx, my, p.label, 6.2, anc, p.color))
            way = "".join(f'<mxPoint x="{x}" y="{y}"/>' for x, y in p.pts[1:-1])
            style = (f"edgeStyle=none;html=1;strokeColor={p.color};"
                     f"endArrow={'block' if p.arrow else 'none'}"
                     + (";dashed=1" if p.dashed else ""))
            cells.append(f'<mxCell id="e{cid}" value="{_x(p.label)}" '
                         f'style="{style}" edge="1" parent="1">'
                         f'<mxGeometry relative="1" as="geometry">'
                         f'<mxPoint x="{p.pts[0][0]}" y="{p.pts[0][1]}" '
                         f'as="sourcePoint"/><mxPoint x="{p.pts[-1][0]}" '
                         f'y="{p.pts[-1][1]}" as="targetPoint"/>'
                         + (f'<Array as="points">{way}</Array>' if way else "")
                         + '</mxGeometry></mxCell>')
            cid += 1

    mx = (f'<mxfile host="app.diagrams.net" type="device">'
          f'<diagram id="{_x(title)}" name="{_x(title)}">'
          f'<mxGraphModel dx="{w}" dy="{h}" grid="0" gridSize="10" guides="1" '
          f'tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" '
          f'pageWidth="{w}" pageHeight="{h}" math="0" shadow="0"><root>'
          f'<mxCell id="0"/><mxCell id="1" parent="0"/>'
          + "".join(cells) +
          f'</root></mxGraphModel></diagram></mxfile>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" width="{w}" '
            f'height="{h}" viewBox="0 0 {w} {h}" '
            f'style="max-width:100%;height:auto" content="{_x(mx)}">'
            f'<defs><marker id="a" markerWidth="6" markerHeight="6" refX="5" '
            f'refY="3" orient="auto"><path d="M0,0 L6,3 L0,6 z" '
            f'fill="{MUTED}"/></marker></defs>'
            f'<rect width="{w}" height="{h}" fill="#ffffff"/>'
            + "".join(body) + '</svg>\n')


def _svg_text(x, y, s, size, anchor, color, bold=False):
    weight = ' font-weight="bold"' if bold else ""
    return (f'<text x="{x}" y="{y}" font-family="Helvetica,Arial,sans-serif" '
            f'font-size="{size}" fill="{color}" text-anchor="{anchor}"{weight}>'
            f'{_x(s)}</text>')


def _x(s):
    return html.escape(str(s), quote=True)


# --- layouts: a model, laid out, becomes primitives --------------------------

W, ROW_H, GAP = 640, 23, 6


def _group(rows):
    """Rows folded to their families, so a diagram of eight trays is one box.
    A row's family is its name up to the last _, which is how the site already
    groups a variable."""
    out, seen = [], {}
    for member, names in rows:
        key = member
        if key not in seen:
            seen[key] = len(out)
            out.append([member, len(names)])
        else:
            out[seen[key]][1] += len(names)
    return out


def _fill_of(names, rows):
    """A member's colour and whether it is retained, from the ROW it names."""
    kind, row = rows.get(names[0], ("", {}))
    keep = kind in RETAINED_KINDS or str(row.get("retain", "")).strip() == "true"
    return KIND_FILL.get(kind, PLAIN), keep


def program_diagram(p, rows):
    """A POU: what it reads on the left, what it holds in the middle, what it
    produces on the right. Derived from the table, so it cannot go stale."""
    ins, outs = [], []
    seen = {}
    for member, sec, row, idx in p["conns"]:
        key = (sec, member)
        if key not in seen:
            seen[key] = [member, sec, []]
            (ins if sec == "in" else outs).append(seen[key])
        seen[key][2].append(row)

    held = [(b["name"] + (f"[{b['count']}]" if b["count"] else ""),
             vars_programs.block_type(b)) for b in p["blocks"]]
    kept = [(v["name"] + (f"[{v['count']}]" if v["count"] else ""), v["type"])
            for v in p["vars"]]

    prims, used = [], set()
    colw, cx = 168, [8, 236, W - 176]
    y0 = 26

    if ins:
        prims.append(text(cx[0], 16, "VAR_INPUT", 7, bold=True))
    if held or kept:
        prims.append(text(cx[1] + colw / 2.0, 16, "what it holds", 7, "middle",
                          bold=True))
    shared = any(sec == "inout" for _m, sec, _n in outs)
    prims.append(text(cx[2] + colw, 16,
                      "VAR_OUTPUT / VAR_IN_OUT" if shared else "VAR_OUTPUT",
                      7, "end", bold=True))

    keeps = False
    for col, items in ((0, ins), (2, outs)):
        for i, (member, sec, names) in enumerate(items):
            y = y0 + i * (ROW_H + GAP)
            n = f"{member}[{len(names)}]" if len(names) > 1 else member
            fill, keep = _fill_of(names, rows)
            used.add(fill)
            keeps = keeps or keep
            sub = _row_label(names) + (" <>" if sec == "inout" else "")
            prims.append(rect(cx[col], y, colw, ROW_H, n, fill, sub=sub,
                              dot=keep))
            if keep:
                prims.append(dot(cx[col] + colw - 5.5, y + 5.5))

    # All three columns start at the same top edge. Centring the middle against
    # the tallest side put four blocks halfway down a page of eighteen rows,
    # with a dashed box stretched to match and mostly empty.
    mid_n  = len(held) + len(kept)
    mid_h  = mid_n * (ROW_H + GAP)
    body_h = max(len(ins), len(outs)) * (ROW_H + GAP)
    if mid_n:
        prims.append(rect(cx[1] - 6, y0 - 6, colw + 12, mid_h + 6, "", PLAIN,
                          dashed=True))
    for i, (name, typ) in enumerate(held + kept):
        y = y0 + i * (ROW_H + GAP)
        prims.append(rect(cx[1], y, colw, ROW_H, name,
                          BLK_C if i < len(held) else VAR_C, sub=typ))

    span = max(body_h, mid_h) + y0
    # ...and the arrows meet the middle where the middle IS, not at the page's
    # own half way.
    wire_y = y0 + (mid_h if mid_n else body_h) / 2.0
    if mid_n:
        if ins:
            prims.append(line([(cx[0] + colw + 4, wire_y),
                               (cx[1] - 10, wire_y)], arrow=True))
        prims.append(line([(cx[1] + colw + 10, wire_y),
                           (cx[2] - 4, wire_y)], arrow=True))
    elif ins:
        prims.append(line([(cx[0] + colw + 4, wire_y),
                           (cx[2] - 4, wire_y)], arrow=True))

    # What the fills mean, on the page that uses them, and only those it does.
    ly, lx = span + 12, 8
    key = [(f, KIND_NAME[f]) for f in
           (FIELD_IN, FIELD_OUT, STATUS_C, PARAM_C, ALARM_C, GLOBAL_C)
           if f in used]
    if held:
        key.append((BLK_C, "function block"))
    if kept:
        key.append((VAR_C, "remembers"))
    for fill, what in key:
        prims.append(rect(lx, ly, 9, 8, "", fill))
        prims.append(text(lx + 13, ly + 7, what, 6.4, color=MUTED))
        lx += 16 + len(what) * 3.4
    if keeps:
        prims.append(dot(lx + 4, ly + 4))
        prims.append(text(lx + 10, ly + 7, "retained", 6.4, color=MUTED))
        lx += 16 + 8 * 3.4
    if shared:
        prims.append(text(lx + 4, ly + 7, "<> read and written", 6.4,
                          color=MUTED))
    return prims, W, int(span + 28)


def _row_label(names):
    if len(names) == 1:
        return names[0]
    head = os.path.commonprefix(names).rstrip("0123456789")
    return f"{head}* x{len(names)}" if head else f"{len(names)} rows"


def flow_diagram(spec):
    """Signal flow, TOP DOWN: one band per stage, wired downward.

    Portrait pages are tall and narrow, so stages go down the page. A wire that
    skips a stage is routed down a CORRIDOR - a vertical lane no box occupies -
    because the straight line between its ends runs through the stage between.
    """
    stages = spec["cols"]
    bw, bh, gapx, gapy, top = 150, 26, 14, 30, 26
    width = max(len(st) for st in stages) * (bw + gapx) - gapx
    prims, pos = [], {}
    y = top
    for r, st in enumerate(stages):
        x0 = (width - (len(st) * (bw + gapx) - gapx)) / 2.0 + 8
        for i, (name, sub, fill) in enumerate(st):
            x = x0 + i * (bw + gapx)
            prims.append(rect(x, y, bw, bh, name, fill, sub=sub))
            pos[name] = (x + bw / 2.0, y, r, x)
        y += bh + gapy
    bottom = y - gapy

    def _free(x, r0, r1):
        """Is the lane at x clear of every box in rows r0..r1?"""
        for _cx, _cy, r, bx0 in pos.values():
            if r0 <= r <= r1 and bx0 - 7 < x < bx0 + bw + 7:
                return False
        return True

    def _corridor(want, r0, r1):
        # The margins first, then out from the target's own lane: a wire wants
        # to arrive straight down on its target if anything lets it.
        for x in [want] + [want + d * s for d in range(8, 200, 8)
                           for s in (-1, 1)]:
            if 4 < x < width + 4 and _free(x, r0, r1):
                return x
        return -4.0

    for a, b, label in spec.get("wires", []):
        if a not in pos or b not in pos:
            continue
        ax, ay, ra, _ax0 = pos[a]
        bx, by, rb, _bx0 = pos[b]
        if rb <= ra:                            # sideways, or a feedback
            lane = ay + bh + gapy / 2.0
            pts = [(ax, ay + bh), (ax, lane), (bx, lane), (bx, by + bh + 2)]
        elif abs(ax - bx) < 1.0 and rb == ra + 1:
            pts = [(ax, ay + bh), (bx, by - 2)]
        elif rb == ra + 1:                      # one gap, and it is empty
            mid = ay + bh + gapy / 2.0
            pts = [(ax, ay + bh), (ax, mid), (bx, mid), (bx, by - 2)]
        else:                                   # around the stages between
            lane = _corridor(bx, ra + 1, rb - 1)
            y_top = ay + bh + gapy / 2.0
            y_bot = by - gapy / 2.0
            pts = [(ax, ay + bh), (ax, y_top), (lane, y_top), (lane, y_bot),
                   (bx, y_bot), (bx, by - 2)]
        out = [pts[0]]
        for q in pts[1:]:                       # a lane ON the target is a line
            if abs(q[0] - out[-1][0]) > 0.5 or abs(q[1] - out[-1][1]) > 0.5:
                out.append(q)
        prims.append(line(out, arrow=True, label=label))
    return prims, int(width + 16), int(bottom + 6)


def states_diagram(spec):
    """A state machine: the states around a ring, the transitions labelled.

    A pair with an edge each way is BOWED apart - drawn straight, the two lie on
    top of each other and so do their labels.
    """
    import math
    nodes = spec["states"]
    n = len(nodes)
    bw, bh = 104, 26
    rx, ry = (150, 96) if n > 2 else (0, 54)
    cx, cy = 250, 30 + ry + bh / 2.0
    prims, pos = [], {}
    for i, (name, sub) in enumerate(nodes):
        a = -math.pi / 2.0 + i * 2.0 * math.pi / n
        x = cx + rx * math.cos(a) - bw / 2.0
        y = cy + ry * math.sin(a) - bh / 2.0
        prims.append(rect(x, y, bw, bh, name, spec.get("fill", BLK_C), sub=sub))
        pos[name] = (x + bw / 2.0, y + bh / 2.0)

    pairs = {(a, b) for a, b, _l in spec["edges"]}
    for a, b, label in spec["edges"]:
        if a not in pos or b not in pos:
            continue
        (x0, y0), (x1, y1) = pos[a], pos[b]
        dx, dy = x1 - x0, y1 - y0
        d = max(1.0, math.hypot(dx, dy))
        ux, uy = dx / d, dy / d
        # Trim to the box EDGE, not to a circle around it: a circle sized for a
        # side-by-side pair leaves a stacked pair floating.
        k = min(abs((bw / 2.0 + 4) / ux) if abs(ux) > 1e-3 else 1e9,
                abs((bh / 2.0 + 4) / uy) if abs(uy) > 1e-3 else 1e9)
        sx, sy = x0 + ux * k, y0 + uy * k
        ex, ey = x1 - ux * k, y1 - uy * k
        if (b, a) in pairs:                      # bow the two apart
            px, py = -uy * 30, ux * 30
            mx, my = (sx + ex) / 2.0 + px, (sy + ey) / 2.0 + py
            prims.append(line([(sx, sy), (mx, my), (ex, ey)], arrow=True))
            prims.append(text(mx + px * 0.5, my + py * 0.5 + 2, label, 6.2,
                              "middle", MUTED))
        else:
            prims.append(line([(sx, sy), (ex, ey)], arrow=True))
            # Beside the line, not on it.
            prims.append(text((sx + ex) / 2.0 - uy * 8,
                              (sy + ey) / 2.0 + ux * 8 - 3, label, 6.2,
                              "middle", MUTED))
    return prims, 500, int(cy + ry + bh)


def curve_diagram(spec):
    """A transfer curve: what comes out for what goes in.

    A hysteretic block has TWO curves, one per direction of travel, and the gap
    between them IS the block - so both are drawn, with an arrow on each saying
    which way the reading was going.
    """
    x0, y0, w, h = 46, 28, 330, 120
    prims = [line([(x0, y0), (x0, y0 + h), (x0 + w, y0 + h)]),
             text(x0 - 6, y0 + 6, spec.get("ylab", "out"), 6.6, "end"),
             text(x0 + w, y0 + h + 12, spec.get("xlab", "in"), 6.6, "end")]

    def _plot(points, falling):
        # The falling branch is nudged clear of the rising one: on the flat runs
        # the two coincide exactly, and one drawn over the other hides the loop.
        dy = 2.5 if falling else 0.0
        col = FALL_C if falling else INK
        pts = [(x0 + px * w, y0 + h - py * h + dy) for px, py in points]
        prims.append(line(pts, color=col))
        # An arrowhead on the longest flat run, pointing the way it is walked.
        best, run = None, -1.0
        for (px, py), (qx, qy) in zip(pts, pts[1:]):
            if abs(qy - py) < 0.5 and abs(qx - px) > run:
                best, run = ((px + qx) / 2.0, py), abs(qx - px)
        if best and run > 24:
            mx, my = best
            d = -9 if falling else 9
            prims.append(line([(mx - d, my), (mx + d, my)], arrow=True,
                              color=col))

    down = spec.get("down")
    _plot(spec["points"], False)
    if down:
        _plot(down, True)
        prims.append(line([(x0 + w - 96, y0 - 4), (x0 + w - 78, y0 - 4)]))
        prims.append(text(x0 + w - 74, y0 - 2, "rising", 6.2, color=MUTED))
        prims.append(line([(x0 + w - 44, y0 - 4), (x0 + w - 26, y0 - 4)],
                          color=FALL_C))
        prims.append(text(x0 + w - 22, y0 - 2, "falling", 6.2, color=MUTED))

    for px, label in spec.get("marks", []):
        prims.append(line([(x0 + px * w, y0), (x0 + px * w, y0 + h)],
                          dashed=True, color=MUTED))
        prims.append(text(x0 + px * w, y0 + h + 10, label, 6.4, "middle"))
    return prims, 400, int(y0 + h + 26)


def timing_diagram(spec):
    """Waveforms: the input, and what the block makes of it."""
    x0, w, lane, y = 76, 300, 30, 30
    prims = []
    for name, wave in spec["traces"]:
        prims.append(text(x0 - 8, y + 12, name, 7, "end"))
        prims.append(line([(x0, y + 18), (x0 + w, y + 18)], dashed=True,
                          color="#cccccc"))
        pts, px = [], x0
        for frac, level in wave:
            nx = x0 + frac * w
            pts.append((px, y + (2 if level else 18)))
            pts.append((nx, y + (2 if level else 18)))
            px = nx
        prims.append(line(pts, color=INK))
        y += lane
    for px, label in spec.get("marks", []):
        prims.append(line([(x0 + px * w, 24), (x0 + px * w, y - 6)],
                          dashed=True))
        prims.append(text(x0 + px * w, y + 8, label, 6.4, "middle"))
    return prims, x0 + w + 20, int(y + 22)


LAYOUT = {"flow": flow_diagram, "states": states_diagram,
          "curve": curve_diagram, "timing": timing_diagram}


# --- the blocks, authored from their sources ---------------------------------
#
# A block's insides are not in any table, so each one is written out here. The
# text beside it says what the source says, in the block's own words.

def _sq(*levels):
    """A square wave from (fraction, level) pairs."""
    return list(levels)


BLOCKS = [
 ("Ton", "timers.h", "IEC TON, on-delay", {
   "kind": "timing",
   "traces": [("in", _sq((0.15, 0), (0.75, 1), (1.0, 0))),
              ("q",  _sq((0.45, 0), (0.75, 1), (1.0, 0)))],
   "marks": [(0.15, "in rises"), (0.45, "+delay"), (0.75, "in drops")],
   "notes": ["q goes true once in has held true for delay_ms, and false the "
             "moment in drops.",
             "elapsed_ms() saturates at the delay, so it reads as a "
             "\"starting in N s\" countdown and never runs away.",
             "Used for: a contact debounce (dc_watch), a generator's cranking "
             "time (ac_local_watch), a state dwell (irrigation)."]}),

 ("Tof", "timers.h", "IEC TOF, off-delay", {
   "kind": "timing",
   "traces": [("in", _sq((0.15, 0), (0.5, 1), (1.0, 0))),
              ("q",  _sq((0.15, 0), (0.8, 1), (1.0, 0)))],
   "marks": [(0.15, "in rises"), (0.5, "in drops"), (0.8, "+delay")],
   "notes": ["q follows in UP immediately and holds true for delay_ms after it "
             "drops. A minimum run time, or an overrun.",
             "Used for: the rain lockout outliving the shower (roof_lockout), "
             "the rot lockout's release (humidity)."]}),

 ("Tp", "timers.h", "IEC TP, pulse", {
   "kind": "timing",
   "traces": [("in", _sq((0.15, 0), (0.3, 1), (0.62, 0), (0.68, 1), (1.0, 0))),
              ("q",  _sq((0.15, 0), (0.55, 1), (1.0, 0)))],
   "marks": [(0.15, "rising edge"), (0.55, "+width")],
   "notes": ["A rising in starts a pulse of exactly width_ms, and the pulse "
             "RUNS TO COMPLETION whatever in does next - a second edge inside "
             "it is ignored.",
             "Used for: nothing now. The thermal screen's traverse was a Tp "
             "until the fabric became a Plc::Actuator, which times the stroke "
             "and knows where it ended."]}),

 ("RTrig / FTrig", "logic.h", "IEC R_TRIG / F_TRIG, edge detectors", {
   "kind": "timing",
   "traces": [("in",    _sq((0.15, 0), (0.55, 1), (1.0, 0))),
              ("RTrig", _sq((0.15, 0), (0.21, 1), (1.0, 0))),
              ("FTrig", _sq((0.55, 0), (0.61, 1), (1.0, 0)))],
   "marks": [(0.15, "rise"), (0.55, "fall")],
   "notes": ["True for exactly ONE activation, on the edge. e = in && !prev "
             "for RTrig; e = !in && prev for FTrig.",
             "An edge detector in front of a signal that is already one scan "
             "wide adds nothing - which is why the screen lost one."]}),

 ("Sr / Rs", "logic.h", "IEC SR / RS, the two latches", {
   "kind": "states",
   "states": [("q = false", "at rest"), ("q = true", "latched")],
   "edges": [("q = false", "q = true", "set"),
             ("q = true", "q = false", "reset")],
   "notes": ["Sr is SET-dominant: q = set || (q && !reset). Both true, and it "
             "latches.",
             "Rs is RESET-dominant: q = !reset && (set || q). Both true, and it "
             "drops. Pick the one whose dominant input is the safe one - a "
             "lockout that must clear on recovery is an Rs."]}),

 ("Hysteresis", "analog.h", "a band with two edges", {
   "kind": "curve",
   "points": [(0.0, 0.05), (0.55, 0.05), (0.55, 0.9), (1.0, 0.9)],
   "down": [(1.0, 0.9), (0.3, 0.9), (0.3, 0.05), (0.0, 0.05)],
   "marks": [(0.3, "low"), (0.55, "high")],
   "xlab": "value", "ylab": "q",
   "notes": ["q goes true at high and false at low, and NOTHING happens in "
             "between: a reading sitting on one threshold cannot chatter.",
             "The two curves never meet between low and high: which one the "
             "block is on is the state it keeps.",
             "Used for: the intake gates (cooling), the dryness gate "
             "(humidity)."]}),

 ("Hysteresis3", "analog.h", "three bands, one reading", {
   "kind": "curve",
   "points": [(0.0, 0.05), (0.3, 0.05), (0.3, 0.45), (0.62, 0.45),
              (0.62, 0.85), (1.0, 0.85)],
   "down": [(1.0, 0.85), (0.46, 0.85), (0.46, 0.45), (0.14, 0.45),
            (0.14, 0.05), (0.0, 0.05)],
   "marks": [(0.14, "e1 - hyst"), (0.3, "edge 1"), (0.46, "e2 - hyst"),
             (0.62, "edge 2")],
   "xlab": "value", "ylab": "level",
   "notes": ["Two thresholds, each with its own hysteresis, giving three "
             "levels. A level change needs the reading to cross a whole band."]}),

 ("Staircase3", "analog.h", "three rungs, each hysteretic", {
   "kind": "curve",
   "points": [(0.0, 0.03), (0.22, 0.03), (0.22, 0.35), (0.5, 0.35),
              (0.5, 0.66), (0.78, 0.66), (0.78, 0.97), (1.0, 0.97)],
   "down": [(1.0, 0.97), (0.64, 0.97), (0.64, 0.66), (0.36, 0.66),
            (0.36, 0.35), (0.08, 0.35), (0.08, 0.03), (0.0, 0.03)],
   "marks": [(0.08, "- hyst"), (0.22, "edge1"), (0.36, "- hyst"),
             (0.5, "edge2"), (0.64, "- hyst"), (0.78, "edge3")],
   "xlab": "value", "ylab": "level 0..3",
   "notes": ["One Hysteresis per rung, each engaging at its edge and releasing "
             "at edge - hyst. Returns how many rungs the reading is above.",
             "Used for: the fan speeds and the destratification bands - which "
             "is why a demand sitting on a rung does not step the relays."]}),

 ("Ramp", "analog.h", "IEC RAMP, a rate limiter", {
   "kind": "curve",
   "points": [(0.0, 0.1), (0.12, 0.1), (0.62, 0.85), (1.0, 0.85)],
   "marks": [(0.12, "target steps")],
   "xlab": "time", "ylab": "out",
   "notes": ["The output moves toward the target by at most rate_per_s, so a "
             "step demand becomes a slope the mechanics can follow.",
             "preset() jumps without ramping, for a mode change."]}),

 ("Deadband", "analog.h", "report only a real move", {
   "kind": "curve",
   "points": [(0.0, 0.2), (0.28, 0.2), (0.28, 0.5), (0.66, 0.5), (0.66, 0.8),
              (1.0, 0.8)],
   "down": [(1.0, 0.8), (0.5, 0.8), (0.5, 0.5), (0.12, 0.5), (0.12, 0.2),
            (0.0, 0.2)],
   "marks": [(0.12, "-band"), (0.28, "+band"), (0.5, "-band"),
             (0.66, "+band")],
   "xlab": "value", "ylab": "reported",
   "notes": ["Holds the last reported figure until the input has moved more "
             "than band from it, then adopts the new one whole.",
             "Suppresses jitter without lagging a real change - a meter's last "
             "digit is not a telemetry publish and not a register write."]}),
]

BLOCKS += [
 ("Ewma", "filters.h", "an exponential average, against dt", {
   "kind": "flow",
   "cols": [[("value", "the reading", IN_C)],
            [("alpha", "dt / (tau + dt)", VAR_C)],
            [("state", "s += alpha * (value - s)", BLK_C)],
            [("out", "s", OUT_C)]],
   "wires": [("value", "state", ""), ("alpha", "state", ""),
             ("state", "out", "")],
   "notes": ["Filtered against the REAL dt, not a fixed blend: a program due "
             "late runs once with the whole elapsed time, and a constant alpha "
             "would silently change the time constant when the scan slipped.",
             "Used for: the probe zones, the DC rail averages, the outlet flow."]}),

 ("MedianOf3", "filters.h", "the middle of three", {
   "kind": "flow",
   "cols": [[("a", "", IN_C), ("b", "", IN_C), ("c", "", IN_C)],
            [("sort", "drop high and low", BLK_C)],
            [("out", "the middle one", OUT_C)]],
   "wires": [("a", "sort", ""), ("b", "sort", ""), ("c", "sort", ""),
             ("sort", "out", "")],
   "notes": ["One spike cannot move the answer, which is what a median buys "
             "over a mean. Stateless."]}),

 ("MovingAverage<N>", "filters.h", "the last N samples, evenly weighted", {
   "kind": "flow",
   "cols": [[("value", "", IN_C)],
            [("ring", "N slots, oldest overwritten", BLK_C)],
            [("out", "sum / count", OUT_C)]],
   "wires": [("value", "ring", ""), ("ring", "out", "")],
   "notes": ["Every sample counts the same, and one leaves the window "
             "entirely - where an Ewma's oldest reading only ever fades.",
             "Fixed storage: N floats, no allocation."]}),

 ("Integral", "meter.h", "a rate becomes a total", {
   "kind": "flow",
   "cols": [[("rate_per_s", "", IN_C), ("cutoff", "", IN_C)],
            [("gate", "|rate| < cutoff -> 0", BLK_C)],
            [("accumulate", "total += rate * dt", BLK_C)],
            [("total", "double, so it does not lose the small ones", OUT_C)]],
   "wires": [("rate_per_s", "gate", ""), ("cutoff", "gate", ""),
             ("gate", "accumulate", ""), ("accumulate", "total", "")],
   "notes": ["The cutoff is a LIVE ZERO: a 4-20 mA loop sitting on its own "
             "noise would otherwise totalize a plausible volume with every "
             "valve shut.",
             "The total is a double - a float loses a small rate against a "
             "large total, which is exactly what a meter does all day."]}),

 ("Derivative", "meter.h", "a total becomes a rate", {
   "kind": "flow",
   "cols": [[("quantity", "", IN_C), ("window_ms", "", IN_C)],
            [("wait", "until elapsed >= window", BLK_C)],
            [("divide", "(q - prev) / elapsed", BLK_C)],
            [("rate", "held between windows", OUT_C)]],
   "wires": [("quantity", "wait", ""), ("window_ms", "wait", ""),
             ("wait", "divide", ""), ("divide", "rate", "")],
   "notes": ["Over the time ACTUALLY accumulated, not the nominal window: a "
             "late scan overshoots it, and dividing by the nominal figure would "
             "report a rate that never happened.",
             "Between windows the last rate stands rather than reading zero."]}),

 ("Pid", "pid.h", "hold a reading at a setpoint", {
   "kind": "flow",
   "cols": [[("sp", "setpoint", IN_C), ("pv", "the reading", IN_C)],
            [("error", "sp - pv, sign-corrected", BLK_C)],
            [("P", "kp * error", BLK_C),
             ("I", "+= ki * error * dt", BLK_C),
             ("D", "-kd * d(pv)/dt", BLK_C)],
            [("sum", "P + I + D", BLK_C)],
            [("clamp", "out_min .. out_max", OUT_C)]],
   "wires": [("sp", "error", ""), ("pv", "error", ""), ("error", "P", ""),
             ("error", "I", ""), ("pv", "D", ""), ("P", "sum", ""),
             ("I", "sum", ""), ("D", "sum", ""), ("sum", "clamp", "")],
   "notes": ["Three departures from the textbook sum, all affecting tuning:",
             "- DERIVATIVE ON THE MEASUREMENT, not the error: a typed setpoint "
             "does not slam the output.",
             "- CONDITIONAL INTEGRATION: the integral stops accumulating while "
             "the output is saturated, so there is no back-calculation gain to "
             "tune.",
             "- The integral is clamped to the OUTPUT span, so it cannot wind "
             "up somewhere the actuator could never follow.",
             "preset() forces the output and makes the integral carry it, for "
             "a bumpless hand-back from manual."]}),
]

BLOCKS += [
 ("Actuator", "actuator.h", "two coils, no feedback, a position ESTIMATE", {
   "kind": "states",
   "states": [("UNKNOWN", "no datum yet"), ("OPENING", "open coil"),
              ("OPEN", "off the seat"), ("CLOSING", "close coil"),
              ("CLOSED", "on the seat")],
   "edges": [("UNKNOWN", "OPENING", "target high"),
             ("UNKNOWN", "CLOSING", "target low"),
             ("OPENING", "OPEN", "stroke ends"),
             ("OPEN", "CLOSING", "target below"),
             ("CLOSING", "CLOSED", "reaches the seat"),
             ("CLOSED", "OPENING", "target above")],
   "notes": ["CLOSED means ON THE SEAT, which only a stroke to the closed end "
             "produces. Anything else at rest is OPEN, with position() for the "
             "detail: a claim about the seal, not a number.",
             "From UNKNOWN it drives to whichever end the target is nearer and "
             "datums there, and that stroke is COMMITTED - it cannot be "
             "interrupted, because a half-finished datum knows nothing.",
             "Enforced: the coils are never both energised; a reversal passes "
             "through reverse_ms with both dropped (a contactor that makes "
             "before the other breaks shorts the motor); a move under "
             "min_run_ms is not made at all.",
             "The estimate drifts, because nothing between the stops can "
             "correct it. Past redatum_travel the next PARTIAL move detours to "
             "the closed seat first and buys a known position for one stroke.",
             "Used for: the four roof windows, and the thermal screen - which "
             "PRESETS rather than datums, because its coil is held and deploys "
             "de-energised, so a released coil already says where the fabric "
             "is."]}),

 ("ValveController", "site: blocks/", "one two-position valve", {
   "kind": "flow",
   "cols": [[("duty", "0..1 demand", IN_C), ("period_ms", "", IN_C),
             ("min_on_ms", "", IN_C)],
            [("on_ms", "duty * period, 0 under min_on", BLK_C),
             ("phase", "phase += dt, wraps", BLK_C)],
            [("coil", "phase < on_ms", OUT_C)]],
   "wires": [("duty", "on_ms", ""), ("period_ms", "phase", ""),
             ("min_on_ms", "on_ms", ""),
             ("on_ms", "coil", ""), ("phase", "coil", "")],
   "notes": ["A pulse train: a fractional demand becomes an ON-TIME within a "
             "period. A pulse under min_on_ms is not made at all, because one "
             "that short does not lift the seat - rounding it up would deliver "
             "water nobody asked for.",
             "park() drives it where a DEAD CONTROLLER would leave it, and the "
             "fail direction is read off the coil ROW's declared default: the "
             "table is the one place it is said.",
             "Used for: the irrigation cycle's ten valves, and the wet skirt."]}),

 ("AcSupply", "site: blocks/", "one AC supply, mirrored and judged", {
   "kind": "flow",
   "cols": [[("volts", "field read", IN_C), ("amps", "field read", IN_C),
             ("watts", "field read", IN_C), ("hz", "field read", IN_C),
             ("lo_V / hi_V", "the voltage band", PARAM_C),
             ("lo_Hz / hi_Hz", "the frequency band", PARAM_C)],
            [("deadband x4", "one per quantity", BLK_C),
             ("in_range", "V and Hz against their bands", BLK_C)],
            [("out_V / out_A / out_W / out_Hz", "the mirror", OUT_C),
             ("ok", "measured and in spec", OUT_C),
             ("fault", "out of spec, or gone silent", OUT_C),
             ("answered", "so the caller can say DEGRADED", OUT_C)]],
   "wires": [("volts", "deadband x4", ""), ("amps", "deadband x4", ""),
             ("watts", "deadband x4", ""), ("hz", "deadband x4", ""),
             ("deadband x4", "out_V / out_A / out_W / out_Hz", ""),
             ("volts", "in_range", ""), ("hz", "in_range", ""),
             ("lo_V / hi_V", "in_range", ""), ("lo_Hz / hi_Hz", "in_range", ""),
             ("in_range", "ok", ""), ("in_range", "fault", ""),
             ("volts", "answered", "")],
   "notes": ["The mirror exists because a field-read row carries no Modbus "
             "register of its own, so an external master could not see the "
             "meter without one.",
             "A meter that has NEVER answered is bring-up, not a fault. Only "
             "one that answered and then stopped raises it.",
             "Bound from either supply's interface by template: the grid and "
             "the generator declare the same member names, and the compiler is "
             "what holds them to it."]}),

 ("ZoneProbes", "site: blocks/", "three probes become one reading", {
   "kind": "flow",
   "cols": [[("t[3]", "three probes", IN_C), ("h[3]", "", IN_C)],
            [("select", "drop the OUTLIER, average the two that agree", BLK_C)],
            [("Ewma x2", "against dt, tau from a parameter", BLK_C)],
            [("temp, rh", "the zone's rows", OUT_C),
             ("quorum", "alarm on too few", OUT_C)]],
   "wires": [("t[3]", "select", ""), ("h[3]", "select", ""),
             ("select", "Ewma x2", ""), ("Ewma x2", "temp, rh", ""),
             ("select", "quorum", "used")],
   "notes": ["Dropping the outlier and averaging the two that agree keeps the "
             "second good probe's information, where a plain median throws it "
             "away.",
             "Only VALID probes take part. With none left the zone's rows go "
             "INVALID rather than publishing a number nothing measured."]}),
]


# --- FBD: the network a program declares, drawn --------------------------------

# Pin names are the block's OWN parameter names, not IEC's: this report explains
# this code, and these are what a reader finds in the .cpp.
is_function = vars_programs.is_function


def _pins(type_name):
    """The pin lists for a node type. Unknown is one in and one out, which is
    what an undeclared type looks like - check_net is what refuses it."""
    return vars_programs.pins_of(type_name) or (("in",), ("out",))


def net_spec(p):
    """One program's [net] as the drawing wants it.

    A `=` source is not a signal at all: the prefix comes off and what is left
    is drawn as a left-hand stub, which is where a literal or a constant goes.
    """
    return [(n["name"], vars_programs.net_type(n, p["blocks"]),
             {k: (v[1:] if v.startswith("=") else v) for k, v in n["in"].items()},
             dict(n["out"]))
            for n in p["net"]]


def fbd_diagram(spec, unwired=None):
    """An FBD: what a program holds and calls, wired between its own variables.

    Laid out by DEPTH - a node sits one column right of whatever feeds it - so
    the page reads left to right the way a signal does. Every node, pin and
    source comes from the program's [net]; check_net is what holds that to the
    program's own locals.
    """
    inst = {b[0]: b for b in spec}
    if unwired is None:
        unwired = []

    # Which node publishes which row. A law that READS BACK a row it has just
    # written - because that row is what an operator can force - is connected to
    # itself, and the layout has to know that as much as the drawing does.
    made = {}
    for _n, _t, _i, outs in spec:
        for _pin, dst in outs.items():
            made.setdefault(str(dst), (_n, _pin))

    def _feeds(name):
        """Every node this one reads: as a bare source, from inside a formula,
        or through a row that another node publishes. One answer, so what the
        columns are ordered by and what the wires are drawn from agree."""
        out = set()
        for src in inst[name][2].values():
            body = str(src)[1:] if str(src).startswith("=") else str(src)
            for m in re.finditer(r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)", body):
                if m.group(1) in inst:
                    out.add(m.group(1))
            if body in made:
                out.add(made[body][0])
        return out - {name}

    # A program's [net] is often SEVERAL networks - four rails that never meet,
    # four zones, a fill path and a drain path. Drawn as one graph they share
    # columns they have no reason to share and their wires cross each other for
    # nothing, so each is found here and laid out on its own rows.
    adj = {n: set() for n in inst}
    for name in inst:
        for head in _feeds(name):
            adj[name].add(head)
            adj[head].add(name)
    comps, taken = [], set()
    for b in spec:                       # spec order, so the table's order holds
        if b[0] in taken:
            continue
        stack, here = [b[0]], set()
        while stack:
            n = stack.pop()
            if n in taken:
                continue
            taken.add(n)
            here.add(n)
            stack.extend(adj[n] - taken)
        comps.append([x for x in spec if x[0] in here])

    # The left rail carries a stub per pin nothing else feeds, and a formula is
    # a long label. Measured up front, because it decides where column 0 sits.
    rail = []
    for _n, _t, _ins, _o in spec:
        for _pin in _pins(_t)[0]:
            _src = str(_ins.get(_pin, ""))
            _body = _src[1:] if _src.startswith("=") else _src
            _ref = re.match(r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)", _src)
            if (_ref and _ref.group(1) in inst) or _body in made:
                continue                  # a wire carries it, not a stub
            rail.append(_body)
    bw, gapx, gapy = 118, 104, 22
    lane = max(96, int(max((len(t) for t in rail), default=0) * 3.4) + 22)
    # A deep network WRAPS rather than running off the side: past three columns
    # the page is scaled down to fit and the pin names stop being readable,
    # which is the one thing the diagram is for.
    BAND = 3
    prims, box, y_of, bottom = [], {}, {}, {}
    top, widest = 26, 1
    for comp in comps:
        mine = set(x[0] for x in comp)
        depth = {}

        def _depth(name, seen, mine=mine, depth=depth):
            if name in depth:
                return depth[name]
            if name in seen:
                return 0                   # a loop, which a scan may legally have
            d = 0
            for head in _feeds(name):
                if head in mine:
                    d = max(d, _depth(head, seen | {name}) + 1)
            depth[name] = d
            return d

        for n in mine:
            _depth(n, set())
        cols = max(depth.values()) + 1
        widest = max(widest, min(cols, BAND))
        band_top = top
        for c in range(cols):
            here = [x for x in comp if depth[x[0]] == c]
            if c and c % BAND == 0:                  # a new band starts below
                band_top = max((y_of[n][1] + y_of[n][3] for n in mine
                                if n in y_of), default=band_top) + 58
            y = band_top
            for name, typ, ins, outs in here:
                pi, po = _pins(typ)
                bh = 16 + max(len(pi), len(po)) * 11
                x = lane + (c % BAND) * (bw + gapx)
                prims.append(rect(x, y, bw, bh, "",
                                  FN_C if is_function(typ) else BLK_C))
                label = typ.split("::")[-1]
                if is_function(typ):
                    # A function has no instance to name - it is not held, and
                    # two calls to it are the same function. IEC spells them in
                    # caps, and that is what tells it from a block at a glance.
                    prims.append(text(x + bw / 2.0, y + 10, label.upper(),
                                      6.6, "middle", INK))
                else:
                    prims.append(text(x + bw / 2.0, y - 3, name, 6.6, "middle",
                                      bold=True))
                    prims.append(text(x + bw / 2.0, y + 10, label, 6.6,
                                      "middle", INK))
                for i, pin in enumerate(pi):
                    py = y + 20 + i * 11
                    prims.append(text(x + 4, py, pin, 5.8, color=MUTED))
                    box[(name, "in", pin)] = (x, py - 2)
                for i, pin in enumerate(po):
                    py = y + 20 + i * 11
                    prims.append(text(x + bw - 4, py, pin, 5.8, "end",
                                      color=MUTED))
                    box[(name, "out", pin)] = (x + bw, py - 2)
                y_of[name] = (x, y, bw, bh)
                y += bh + gapy
        foot = max((y_of[n][1] + y_of[n][3] for n in mine if n in y_of),
                   default=top)
        for n in mine:
            bottom[n] = foot          # this network's own channel floor
        top = foot + 46
    height = max((v[1] + v[3] for v in y_of.values()), default=60)

    def _hits(x0, x1, y0, y1):
        """Whether the axis-aligned run (x0,y0)-(x1,y1) touches any box."""
        x0, x1 = min(x0, x1), max(x0, x1)
        y0, y1 = min(y0, y1), max(y0, y1)
        for bx, by, bwid, bhgt in y_of.values():
            if x0 < bx + bwid and x1 > bx and y0 < by + bhgt and y1 > by:
                return True
        return False

    jogs = {}

    def _route(sx, sy, tx, ty, floor_y, lanes):
        """A wire from an output pin to an input pin, around the boxes.

        The straight three-segment run first, which is what an adjacent pair of
        columns wants. A wire spanning further crosses whatever column it flies
        over, so that one drops into a CHANNEL under the diagram, runs across
        and climbs back - one lane per wire, so two never sit on top of another.
        """
        # Each wire jogs at its OWN x inside the gutter. Sharing one midpoint
        # put two verticals on top of each other wherever a pin fed two nodes,
        # and the pair read as a single wire going somewhere neither went.
        n = jogs.get((sx, sy), 0)
        jogs[(sx, sy)] = n + 1
        if abs(sy - ty) < 0.1 and not _hits(sx, tx, sy, sy):
            return [(sx, sy), (tx, ty)]     # level with each other: no jog at all
        mid = max(sx + 9, sx + (tx - sx) / 2.0 + 9 * (n % 5) - 18)
        if not (_hits(sx, mid, sy, sy) or _hits(mid, mid, sy, ty)
                or _hits(mid, tx, ty, ty)):
            return [(sx, sy), (mid, sy), (mid, ty), (tx, ty)]
        gx = sx + gapx / 2.0          # the gutter just right of the source
        # Under THIS network, not under the page: a wire that dived past every
        # other network below it was the length of the diagram for nothing.
        n = lanes.setdefault(floor_y, 0)
        lanes[floor_y] = n + 1
        ch = floor_y + 12 + 7 * (n % 12)
        return [(sx, sy), (gx, sy), (gx, ch), (tx - 10, ch),
                (tx - 10, ty), (tx, ty)]

    # The wires. A source is either another block's output pin or a variable,
    # and a variable gets a stub on the left rail so the page names it once.
    left, right, lanes = [], [], {}
    for name, typ, ins, outs in spec:
        for pin, src in ins.items():
            if (name, "in", pin) not in box:
                continue
            tx, ty = box[(name, "in", pin)]
            # Every node output the source names, whether the source IS one or
            # merely mentions one: a formula over another node's output is a
            # connection, and a page that drew only the bare form left two
            # boxes looking independent when one feeds the other.
            body = str(src)[1:] if str(src).startswith("=") else str(src)
            fed = [m for m in re.finditer(r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)", body)
                   if m.group(1) in inst]
            if fed and body != fed[0].group(0):
                for m in fed:
                    h, opin = m.group(1), m.group(2)
                    if (h, "out", opin) not in box:
                        opin = _pins(inst[h][1])[1][0]
                    if (h, "out", opin) in box:
                        sx, sy = box[(h, "out", opin)]
                        prims.append(line(_route(sx, sy, tx, ty,
                                                 bottom.get(name, height), lanes),
                                          arrow=True))
                left.append((body, tx, ty))
                continue
            if str(src) in made:
                h, opin = made[str(src)]
                if h != name and (h, "out", opin) in box:
                    sx, sy = box[(h, "out", opin)]
                    prims.append(line(_route(sx, sy, tx, ty,
                                             bottom.get(name, height), lanes),
                                      arrow=True))
                    continue
            ref = re.match(r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)", str(src))
            head = ref.group(1) if ref else str(src)
            if head in inst:
                # The named pin, or the block's first output when the source is
                # an expression over it ("flow_avg.out / 60").
                opin = ref.group(2) if ref else _pins(inst[head][1])[1][0]
                if (head, "out", opin) not in box:
                    opin = _pins(inst[head][1])[1][0]
                if (head, "out", opin) in box:
                    sx, sy = box[(head, "out", opin)]
                    prims.append(line(_route(sx, sy, tx, ty,
                                             bottom.get(name, height), lanes),
                                      arrow=True))
                else:
                    unwired.append(f"{name}.{pin} <- {src}")
                continue
            left.append((src, tx, ty))
        for pin, dst in outs.items():
            if (name, "out", pin) in box:
                sx, sy = box[(name, "out", pin)]
                right.append((dst, sx, sy))

    # Beside ITS OWN block, never on one shared rail: a stub for a block two
    # columns in would otherwise be drawn at the far left, and its wire would
    # cross everything between.
    for label, tx, ty in left:
        prims.append(text(tx - 10, ty + 2, label, 6.0, "end", INK))
        prims.append(line([(tx - 8, ty), (tx, ty)], arrow=True))
    # ...and the same on the way out: a stub at the far right would be drawn
    # straight through every column after this block's own.
    edge = lane + widest * (bw + gapx) - gapx
    for label, sx, sy in right:
        # A pin can feed a node AND a row. Drawn straight, the stub lies on top
        # of the wire for its first few pixels and the two read as one, so the
        # stub steps aside when the pin is already wired somewhere.
        if (sx, sy) in jogs:
            prims.append(line([(sx, sy), (sx + 5, sy), (sx + 5, sy + 6),
                               (sx + 13, sy + 6)], arrow=True))
            prims.append(text(sx + 17, sy + 8, label, 6.0, color=INK))
        else:
            prims.append(line([(sx, sy), (sx + 8, sy)], arrow=True))
            prims.append(text(sx + 12, sy + 2, label, 6.0, color=INK))
    deepest = max((f + 12 + 7 * 11 for f in lanes), default=height)
    return prims, int(edge + 120), int(max(height, deepest) + 14)




# --- the document ------------------------------------------------------------

def _rows_of(site):
    import vars_checks
    import vars_gen
    both = []
    for d, name in ((os.path.join(site, "tables"), os.path.basename(site)),
                    (os.path.join(ROOT, "src", "libbotanik", "vars", "tables"),
                     "libbotanik")):
        for items in vars_gen.load(d, name).values():
            both += items
    eff, _ = vars_checks._shadows(both, lambda _m: None)
    return {r["name"]: (k, r) for k, r in eff}


def _wrap(s, n):
    out, line_ = [], ""
    for word in s.split():
        if len(line_) + len(word) + 1 > n:
            out.append(line_)
            line_ = word
        else:
            line_ = (line_ + " " + word).strip()
    if line_:
        out.append(line_)
    return out


def page(c, title, subtitle, prims, dw, dh, notes, kicker=""):
    """One diagram, one page: what it is, the picture, and what the picture
    does not say."""
    from reportlab.lib.pagesizes import A4
    pw, ph = A4
    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 15)
    c.drawString(40, ph - 52, title)
    c.setFillColor(MUTED)
    c.setFont("Helvetica", 9)
    c.drawString(40, ph - 66, subtitle)
    if kicker:
        c.setFont("Helvetica-Oblique", 8)
        for i, l in enumerate(_wrap(kicker, 108)):
            c.drawString(40, ph - 82 - i * 10, l)
        top = ph - 92 - len(_wrap(kicker, 108)) * 10
    else:
        top = ph - 86
    scale = min(1.0, (pw - 80) / float(dw))
    to_pdf(c, prims, 40, top, scale)
    y = top - dh * scale - 18
    c.setFillColor(INK)
    c.setFont("Helvetica", 8)
    for n in notes:
        for l in _wrap(n, 112):
            if y < 46:
                break
            c.drawString(40, y, l)
            y -= 10.5
        y -= 3
    c.setFillColor(MUTED)
    c.setFont("Helvetica", 7)
    c.drawRightString(pw - 40, 30, title)
    c.showPage()


def _audit(prims, where, bad):
    """No wire may run through the inside of a box. Cheap to check, and the one
    fault that makes a diagram say something false."""
    boxes = [(p.x, p.y, p.w, p.h) for p in prims if p.kind == "rect"]
    for p in prims:
        if p.kind != "line":
            continue
        for a, b in zip(p.pts, p.pts[1:]):
            x0, x1 = sorted((a[0], b[0]))
            y0, y1 = sorted((a[1], b[1]))
            for bx, by, bw, bh in boxes:
                if x0 < bx + bw - 1 and x1 > bx + 1 and \
                   y0 < by + bh - 1 and y1 > by + 1:
                    bad.append(f"{where}: a wire crosses a box at "
                               f"({bx:.0f}, {by:.0f})")


def main():
    from reportlab.lib.pagesizes import A4
    from reportlab.pdfgen import canvas as rl

    progs = vars_programs.load(SITE)
    rows = _rows_of(SITE)
    vars_programs.expand(progs, rows, lambda _m: None)

    os.makedirs(IMAGES, exist_ok=True)
    out = os.path.join(ROOT, "docs", "generated", "gh_mlb41_logic.pdf")
    c = rl.canvas.Canvas(out, pagesize=A4) if hasattr(rl, "canvas") else \
        rl.Canvas(out, pagesize=A4)

    pw, ph = A4
    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 22)
    c.drawString(40, ph - 120, "gh_mlb41: what each POU does inside")
    c.setFillColor(MUTED)
    c.setFont("Helvetica", 10)
    for i, l in enumerate(_wrap(
            "One page per control program and one per function block. A "
            "program gets what it is CONNECTED to, derived from the table that "
            "declares it, and an FBD of the network it "
            "owns, whose instances and types are the table's and whose wiring "
            "is authored from the call site. A block's own page is authored "
            "too, because what a Pid does inside is not in any table. Every "
            "diagram is also an editable images/logic/*.drawio.svg, drawn from "
            "the same model as the page.", 96)):
        c.drawString(40, ph - 150 - i * 13, l)
    c.setFont("Helvetica", 9)
    c.drawString(40, 60, f"{len(progs)} programs, {len(BLOCKS)} blocks. "
                         f"One-time report: scripts/logic_report.py")
    c.showPage()

    made, crossings, unwired = 0, [], []
    for p in progs:
        prims, dw, dh = program_diagram(p, rows)
        notes = [p.get("desc", "")]
        notes += [" ".join(x.split()) for x in
                  (p.get("notes") or "").strip().split("\n\n") if x.strip()]
        if p["fns"]:
            notes.append("Its own helpers: " +
                         ", ".join(f["name"] + "()" for f in p["fns"]) + ".")
        hooks = ["run()"] + [h + "()" for h in p["hooks"]]
        sub = (f"{p['class']} - {p['task']}, {p['stage']} stage - "
               f"defines {', '.join(hooks)}")
        page(c, p["name"], sub, prims, dw, dh, notes)
        _svg(f"{p['name']}", prims, dw, dh)
        made += 1

        wiring = net_spec(p)
        if wiring:
            fprims, fdw, fdh = fbd_diagram(wiring, unwired)
            _audit(fprims, p["name"] + " (fbd)", crossings)
            page(c, p["name"] + ": the network", sub, fprims, fdw, fdh, [],
                 kicker="What it HOLDS and what it CALLS, wired. Blocks are the "
                        "table's; a GREEN box is a stateless function, which no "
                        "table declares. What reaches a pin is authored from the "
                        "call site, because only the .cpp says. Pin names are "
                        "the block's own parameters.")
            _svg(p["name"] + "_fbd", fprims, fdw, fdh)
            made += 1

    for name, where, what, spec in BLOCKS:
        prims, dw, dh = LAYOUT[spec["kind"]](spec)
        _audit(prims, name, crossings)
        page(c, name, f"{where} - {what}", prims, dw, dh, spec.get("notes", []))
        _svg("block_" + name.split()[0].replace("<", "").replace(">", "")
             .replace("/", ""), prims, dw, dh)
        made += 1

    c.save()
    for msg in crossings:
        print("  WARNING " + msg)
    for msg in unwired:
        print("  WARNING a pin names an output that does not exist: " + msg)
    print(f"  {os.path.relpath(out, ROOT)}: written ({made} diagrams)")
    print(f"  {os.path.relpath(IMAGES, ROOT)}/: {made} editable .drawio.svg")


def _svg(name, prims, w, h):
    with open(os.path.join(IMAGES, f"{name}.drawio.svg"), "w") as f:
        f.write(to_svg(prims, w, h, name))


if __name__ == "__main__":
    main()
