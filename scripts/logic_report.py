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

import argparse
import html
import os
import re
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vars_programs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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


def rect(x, y, w, h, label="", fill=PLAIN, sub="", dashed=False, dot=False,
         key=None):
    """@p key names the box so a wire can be ATTACHED to it: drawio wants an
    edge to reference the cells it joins, not two loose points."""
    return Prim("rect", x=x, y=y, w=w, h=h, label=label, fill=fill, sub=sub,
                dashed=dashed, dot=dot, key=key)


def dot(x, y, r=2.2):
    return Prim("dot", x=x, y=y, r=r)


def text(x, y, s, size=8, anchor="start", color=INK, bold=False):
    return Prim("text", x=x, y=y, s=s, size=size, anchor=anchor, color=color,
                bold=bold)


def line(pts, arrow=False, label="", dashed=False, color=MUTED,
         frm=None, to=None):
    """@p frm and @p to are (box key, relative x, relative y): where on which
    box each end hangs, so the editor keeps them joined when a box moves."""
    return Prim("line", pts=pts, arrow=arrow, label=label, dashed=dashed,
                color=color, frm=frm, to=to)


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
    body, cells = [], []
    # Ids first, because an edge has to name cells that may be emitted after
    # it. A box with a key is what a wire attaches to.
    ids, by_key, cid = {}, {}, 2
    for p in prims:
        ids[id(p)] = cid
        if p.kind == "rect" and getattr(p, "key", None) is not None:
            by_key[p.key] = cid
        cid += 1
    for p in prims:
        cid = ids[id(p)]
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
            val = _x(p.label)
            if p.sub:
                val += "&lt;br&gt;&lt;font style=&quot;font-size:6px&quot;&gt;"
                val += _x(p.sub) + "&lt;/font&gt;"
            cells.append(f'<mxCell id="n{cid}" value="{val}" '
                         f'style="{style}" vertex="1" parent="1">'
                         f'<mxGeometry x="{p.x}" y="{p.y}" width="{p.w}" '
                         f'height="{p.h}" as="geometry"/></mxCell>')
        elif p.kind == "dot":
            body.append(f'<circle cx="{p.x}" cy="{p.y}" r="{p.r}" '
                        f'fill="{INK}"/>')
            cells.append(f'<mxCell id="d{cid}" value="" '
                         f'style="ellipse;fillColor={INK};strokeColor=none" '
                         f'vertex="1" parent="1"><mxGeometry x="{p.x - p.r}" '
                         f'y="{p.y - p.r}" width="{p.r * 2}" '
                         f'height="{p.r * 2}" as="geometry"/></mxCell>')
        elif p.kind == "text":
            body.append(_svg_text(p.x, p.y, p.s, p.size, p.anchor, p.color,
                                  p.bold))
            # SVG anchors text at a POINT; drawio aligns it inside a BOX. The
            # box has to be placed so the two land in the same place, or every
            # right- and centre-anchored label - which is most of them - moves
            # the moment the file is opened for editing.
            tw = max(20.0, len(p.s) * p.size * 0.55 + 4)
            al = {"middle": "center", "end": "right"}.get(p.anchor, "left")
            tx = {"center": p.x - tw / 2.0, "right": p.x - tw}.get(al, p.x)
            th = p.size + 4
            style = (f"text;html=1;whiteSpace=nowrap;fontSize={round(p.size)};"
                     f"fontColor={p.color};align={al};verticalAlign=middle"
                     + (";fontStyle=1" if p.bold else ""))
            cells.append(f'<mxCell id="n{cid}" value="{_x(p.s)}" style="{style}" '
                         f'vertex="1" parent="1">'
                         f'<mxGeometry x="{round(tx, 2)}" '
                         f'y="{round(p.y - 0.75 * p.size - 2, 2)}" '
                         f'width="{round(tw, 2)}" '
                         f'height="{round(th, 2)}" as="geometry"/></mxCell>')
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
            joins = ""
            frm, to = getattr(p, "frm", None), getattr(p, "to", None)
            if frm and frm[0] in by_key:
                style += f";exitX={frm[1]};exitY={round(frm[2], 4)};exitDx=0;exitDy=0"
                joins += f' source="n{by_key[frm[0]]}"'
            if to and to[0] in by_key:
                style += f";entryX={to[1]};entryY={round(to[2], 4)};entryDx=0;entryDy=0"
                joins += f' target="n{by_key[to[0]]}"'
            style = style
            cells.append(f'<mxCell id="e{cid}" value="{_x(p.label)}" '
                         f'style="{style}" edge="1" parent="1"{joins}>'
                         f'<mxGeometry relative="1" as="geometry">'
                         f'<mxPoint x="{p.pts[0][0]}" y="{p.pts[0][1]}" '
                         f'as="sourcePoint"/><mxPoint x="{p.pts[-1][0]}" '
                         f'y="{p.pts[-1][1]}" as="targetPoint"/>'
                         + (f'<Array as="points">{way}</Array>' if way else "")
                         + '</mxGeometry></mxCell>')

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
        prims.append(text(cx[1] + colw / 2.0, 16, "VAR", 7, "middle",
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
            prims.append(rect(x, y, bw, bh, name, fill, sub=sub, key=name))
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
        e_rel = (a, round((ax - _ax0) / bw, 4), 1.0)
        t_rel = (b, round((bx - _bx0) / bw, 4), 1.0 if rb <= ra else 0.0)
        out = [pts[0]]
        for q in pts[1:]:                       # a lane ON the target is a line
            if abs(q[0] - out[-1][0]) > 0.5 or abs(q[1] - out[-1][1]) > 0.5:
                out.append(q)
        prims.append(line(out, arrow=True, label=label, frm=e_rel, to=t_rel))
    return prims, int(width + 16), int(bottom + 6)


def states_diagram(spec):
    """A state machine: the states around a ring, the transitions labelled.

    A pair with an edge each way is BOWED apart - drawn straight, the two lie on
    top of each other and so do their labels.
    """
    import math
    nodes = spec["states"]
    # A state nothing RETURNS to is where the machine is entered, not a stop on
    # its cycle: on the ring it forces its edges across the middle, and the
    # Actuator's two crossed the cycle's own. It goes in the HUB instead, and
    # what is left is the cycle, in order, around the ring.
    entered = {b for _a, b, _l in spec["edges"]}
    hub = [x for x in nodes if x[0] not in entered]
    ring = [x for x in nodes if x[0] in entered]
    if not ring or len(hub) > 1:
        hub, ring = [], nodes
    n = len(ring)
    bw, bh = 104, 26
    rx, ry = (150, 96) if n > 2 else (0, 54)
    cx, cy = 250, 30 + ry + bh / 2.0
    prims, pos = [], {}
    for i, (name, sub) in enumerate(ring):
        a = -math.pi / 2.0 + i * 2.0 * math.pi / n
        x = cx + rx * math.cos(a) - bw / 2.0
        y = cy + ry * math.sin(a) - bh / 2.0
        prims.append(rect(x, y, bw, bh, name, spec.get("fill", BLK_C), sub=sub,
                          key=name))
        pos[name] = (x + bw / 2.0, y + bh / 2.0)
    for name, sub in hub:
        x, y = cx - bw / 2.0, cy - bh / 2.0
        prims.append(rect(x, y, bw, bh, name, spec.get("fill", BLK_C), sub=sub,
                          key=name))
        pos[name] = (cx, cy)

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
        fa = (a, round(0.5 + ux * 0.5, 4), round(0.5 + uy * 0.5, 4))
        ta = (b, round(0.5 - ux * 0.5, 4), round(0.5 - uy * 0.5, 4))
        if (b, a) in pairs:                      # bow the two apart
            px, py = -uy * 30, ux * 30
            mx, my = (sx + ex) / 2.0 + px, (sy + ey) / 2.0 + py
            prims.append(line([(sx, sy), (mx, my), (ex, ey)], arrow=True,
                              frm=fa, to=ta))
            prims.append(text(mx + px * 0.5, my + py * 0.5 + 2, label, 6.2,
                              "middle", MUTED))
        else:
            prims.append(line([(sx, sy), (ex, ey)], arrow=True,
                              frm=fa, to=ta))
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
        # Both branches on the SAME levels: a level is a level whichever way the
        # reading arrived at it, and nudging one clear drew a staircase whose
        # treads did not line up. The loop still shows - the two step at
        # different values, which is the whole of what hysteresis is - and the
        # falling branch goes down first so the rising one stays on top.
        col = FALL_C if falling else INK
        pts = [(x0 + px * w, y0 + h - py * h) for px, py in points]
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
    if down:
        _plot(down, True)
    _plot(spec["points"], False)
    if down:
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


ROLE_C = {"in": IN_C, "param": PARAM_C, "block": BLK_C, "out": OUT_C,
          "status": STATUS_C, "alarm": ALARM_C, "var": VAR_C}


def catalogue(blocks, where):
    """A blocks.toml as pages to draw.

    Every block is declared beside the code that implements it - the library's
    with the library, a site's with the site - and this is the one place that
    turns such a declaration into a page. Nothing here knows which it was.
    """
    out = []
    for name, spec in blocks.items():
        inside = spec.get("inside")
        if not inside:
            continue                       # pins only: nothing to draw
        page = dict(inside)
        page["cols"] = [[(n, sub, ROLE_C.get(role, BLK_C))
                         for n, sub, role in col]
                        for col in inside.get("cols", [])]
        for k in ("wires", "edges", "states", "marks", "points", "down"):
            if k in inside:
                page[k] = [tuple(x) for x in inside[k]]
        if "traces" in inside:
            page["traces"] = [(n, [tuple(q) for q in pts])
                              for n, pts in inside["traces"]]
        page["notes"] = list(spec.get("notes", []))
        out.append((name, spec.get("header", where), spec.get("brief", ""),
                    page))
    return out


def all_blocks(site_dir):
    """Every block worth a page: the library's, then the site's own."""
    lib = []
    for path in vars_programs.LIB_BLOCKS:
        lib += catalogue(vars_programs.read_blocks(path), "libbotanik")
    return lib + catalogue(vars_programs.site_blocks(site_dir),
                           "site: blocks/")


LAYOUT = {"flow": flow_diagram, "states": states_diagram,
          "curve": curve_diagram, "timing": timing_diagram}


# --- the blocks, authored from their sources ---------------------------------
#
# A block's insides are not in any table, so each one is written out here. The
# text beside it says what the source says, in the block's own words.

def _sq(*levels):
    """A square wave from (fraction, level) pairs."""
    return list(levels)




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

    def _reads(name, src):
        """The (node, out-pin) pairs one source reads, and its label.

        A source names them three ways and all three are connections: it IS a
        node's pin, it MENTIONS one inside a formula, or it names a ROW that
        another node publishes - which a formula may do too, and roof_lockout's
        wind_lock did, leaving the node that decides it feeding nothing.
        """
        body = str(src)[1:] if str(src).startswith("=") else str(src)
        out = []
        for m in re.finditer(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)?", body):
            tok = m.group(0)
            head, _, tail = tok.partition(".")
            if head in inst and tail and head != name:
                out.append((head, tail))
            elif tok in made and made[tok][0] != name:
                out.append(made[tok])
        return body, out

    def _feeds(name):
        """Every node this one reads. One answer, so what the columns are
        ordered by and what the wires are drawn from agree."""
        out = set()
        for src in inst[name][2].values():
            out |= {h for h, _p in _reads(name, src)[1]}
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

    def _stubs(name):
        """The labels this node hangs off the rail: every input pin no wire
        feeds. They decide how much room the column in front of it needs."""
        out = []
        for pin in _pins(inst[name][1])[0]:
            src = str(inst[name][2].get(pin, ""))
            body = src[1:] if src.startswith("=") else src
            ref = re.match(r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)", src)
            if (ref and ref.group(1) in inst) or body in made:
                continue                  # a wire carries it, not a stub
            out.append(body)
        return out

    bw, gapy = 118, 22

    # Depths first, per network, because the COLUMNS have to be sized before
    # anything is placed: a column is as wide as the longest label landing in
    # front of it, or the two collide and the page has to be read twice.
    depth = {}
    for comp in comps:
        mine = set(x[0] for x in comp)
        d = {}

        def _depth(name, seen, mine=mine, d=d):
            if name in d:
                return d[name]
            if name in seen:
                return 0               # a loop, which a scan may legally have
            v = 0
            for head in _feeds(name):
                if head in mine:
                    v = max(v, _depth(head, seen | {name}) + 1)
            d[name] = v
            return v

        for n in mine:
            _depth(n, set())
        depth.update(d)

    def _widest(c):
        return max((len(t) for n in inst if depth[n] == c for t in _stubs(n)),
                   default=0)

    cols = max(depth.values()) + 1 if depth else 1
    lab = [_widest(c) for c in range(cols)]

    def _slots(band):
        """Where each column SLOT starts. Sized by slot and not by column: a
        network deep enough to wrap puts column 3 back in slot 0, and its
        labels hang off the same rail as column 0's.
        """
        def _for(k):
            return max([lab[c] for c in range(cols) if c % band == k],
                       default=0)
        # A gutter holds the labels landing in front of the column AND the
        # wires crossing it to get there: the labels plus room to pass them.
        out = [max(96, int(_for(0) * 3.4) + 22)]
        for k in range(1, band):
            # A column every wire reaches needs room for the wires and nothing
            # else; one with labels in front of it needs room for those too.
            room = 64 if _for(k) == 0 else max(104, int(_for(k) * 3.4) + 72)
            out.append(out[-1] + bw + room)
        return out

    # Wrap sooner when the columns are wide: a page past about 760 is scaled
    # down to fit and the pin names stop being readable.
    # TWO columns, and a network deeper than that continues on the next row.
    # A connector carries it there, so the wrap costs a name rather than a line
    # back across the page - and the page stays narrow enough to read.
    BAND = 2
    xs = _slots(BAND)
    lane = xs[0]

    prims, box, y_of, bottom, band_of = [], {}, {}, {}, {}
    top, widest = 26, 1
    for comp in comps:
        mine = set(x[0] for x in comp)
        # Vertical ORDER inside each column, so the wires between two columns
        # run flat instead of crossing: a node sits level with whatever feeds
        # it, and a node in the first column level with whatever it feeds.
        # Forward once, then back, which is the usual two sweeps.
        ccols = max(depth[n] for n in mine) + 1
        rank = {n: i for i, n in enumerate(x[0] for x in comp)}
        by_col = [[n for n in rank if depth[n] == c] for c in range(ccols)]
        for _pass in range(2):
            for c in range(1, ccols):
                by_col[c].sort(key=lambda n: (
                    sum(rank[h] for h in _feeds(n) if h in rank) /
                    max(1, len([h for h in _feeds(n) if h in rank])), rank[n]))
                for i, n in enumerate(by_col[c]):
                    rank[n] = i
            for c in range(ccols - 2, -1, -1):
                eats = {n: [m for m in by_col[c + 1] if n in _feeds(m)]
                        for n in by_col[c]}
                by_col[c].sort(key=lambda n: (
                    sum(rank[m] for m in eats[n]) / max(1, len(eats[n]))
                    if eats[n] else rank[n], rank[n]))
                for i, n in enumerate(by_col[c]):
                    rank[n] = i

        band_top = top
        widest = max(widest, min(ccols, BAND))
        for c in range(ccols):
            here = [inst[n] for n in by_col[c]]
            if c and c % BAND == 0:                  # a new band starts below
                band_top = max((y_of[n][1] + y_of[n][3] for n in mine
                                if n in y_of), default=band_top) + 58
            y = band_top
            for name, typ, ins, outs in here:
                pi, po = _pins(typ)
                bh = 16 + max(len(pi), len(po)) * 11
                x = xs[c % BAND]
                prims.append(rect(x, y, bw, bh, "",
                                  FN_C if is_function(typ) else BLK_C,
                                  key=name))
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
                band_of[name] = c // BAND
                y += bh + gapy
        foot = max((y_of[n][1] + y_of[n][3] for n in mine if n in y_of),
                   default=top)
        for n in mine:
            bottom[n] = foot          # this network's own channel floor
        top = foot + 46
    height = max((v[1] + v[3] for v in y_of.values()), default=60)

    def _at(node, way, pin):
        """(edge x, edge y) of a pin as a FRACTION of its box, which is how an
        attached edge says where it leaves or arrives."""
        if (node, way, pin) not in box or node not in y_of:
            return None
        _px, py = box[(node, way, pin)]
        bx, by, bwid, bhgt = y_of[node]
        return (node, 1 if way == "out" else 0, (py - by) / bhgt)

    # A row label sits in the gutter beside its box and a wire through it is
    # as unreadable as one through the box, so it blocks like one.
    blocking = []

    def _hits(x0, x1, y0, y1):
        """Whether the axis-aligned run (x0,y0)-(x1,y1) touches a box or a
        label."""
        x0, x1 = min(x0, x1), max(x0, x1)
        y0, y1 = min(y0, y1), max(y0, y1)
        for bx, by, bwid, bhgt in list(y_of.values()) + blocking:
            if x0 < bx + bwid and x1 > bx and y0 < by + bhgt and y1 > by:
                return True
        return False

    jogs = {}

    def _straight(sx, sy, tx, ty, clear=0):
        """Whether a plain three-segment wire gets there without a detour."""
        mid = max(sx + 16, clear)
        return not (_hits(sx, mid, sy, sy) or _hits(mid, mid, sy, ty)
                    or _hits(mid, tx, ty, ty)) and tx > mid

    def _route(sx, sy, tx, ty, floor_y, lanes, clear=0):
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
        # Jog EARLY, so the long run happens at the target's height. Halfway
        # across put it at the source's, which is where the next box's own pin
        # labels are - the wire ran straight through them.
        # Where early is blocked, try further along before giving up on a
        # straight run: one box in the way is no reason to leave the lane.
        span = tx - sx
        for mid in ([max(sx + 16, clear) + 9 * (n % 5)]
                    + [sx + span * f for f in (0.3, 0.45, 0.6, 0.75)]
                    + [tx - 12]):
            if mid <= sx + 8 or mid >= tx - 2:
                continue
            if not (_hits(sx, mid, sy, sy) or _hits(mid, mid, sy, ty)
                    or _hits(mid, tx, ty, ty)):
                return [(sx, sy), (mid, sy), (mid, ty), (tx, ty)]
        # Round the outside of whatever is in the way, above or below,
        # whichever side is clear: a wire that dived under the whole page to
        # reach the box just below it read as going somewhere else entirely.
        for lane_y in (ty - 30, ty + 34, sy - 30, sy + 34):
            if abs(lane_y - sy) < 12 or lane_y < 4:
                continue
            gx = max(sx + 52, clear)
            if not (_hits(sx, gx, sy, sy)
                    or _hits(gx, gx, sy, lane_y)
                    or _hits(gx, tx - 10, lane_y, lane_y)
                    or _hits(tx - 10, tx - 10, lane_y, ty)):
                return [(sx, sy), (gx, sy), (gx, lane_y),
                        (tx - 10, lane_y), (tx - 10, ty), (tx, ty)]
        gx = max(sx + 52, clear)      # the gutter just right of the source
        # Under THIS network, not under the page: a wire that dived past every
        # other network below it was the length of the diagram for nothing.
        n = lanes.setdefault(floor_y, 0)
        lanes[floor_y] = n + 1
        ch = floor_y + 12 + 7 * (n % 12)
        return [(sx, sy), (gx, sy), (gx, ch), (tx - 10, ch),
                (tx - 10, ty), (tx, ty)]

    # The rows a pin publishes, placed FIRST. A pin that writes a row and also
    # feeds another node continues FROM THE ROW: the signal is named once and
    # the page shows the reader picking up the same name, rather than two
    # arrows leaving one pin as if they were different things.
    # A signal that leaves this row is a CONNECTOR, not a wire: drawn, it is a
    # line back across the whole page, and every FBD tool names it at both ends
    # instead. The name is the row it publishes, or the pin itself.
    pin_row, crossing = {}, {}
    for name, typ, ins, outs in spec:
        for pin, dst in outs.items():
            pin_row[(name, pin)] = str(dst)
    for name, typ, ins, outs in spec:
        for pin in _pins(typ)[0]:
            for h, opin in _reads(name, str(ins.get(pin, "")))[1]:
                if band_of.get(h) != band_of.get(name):
                    crossing[(h, opin)] = pin_row.get((h, opin),
                                                      f"{h}.{opin}")

    from_label, past_labels = {}, {}
    for name, typ, ins, outs in spec:
        for pin, dst in outs.items():
            if (name, "out", pin) not in box:
                continue
            sx, sy = box[(name, "out", pin)]
            end = sx + 14 + len(str(dst)) * 3.4
            prims.append(line([(sx, sy), (sx + 8, sy)], arrow=True,
                              frm=_at(name, "out", pin)))
            prims.append(text(sx + 12, sy + 2, str(dst), 6.0, color=INK))
            from_label[(name, pin)] = (end, sy)
            blocking.append((sx + 8, sy - 5, end - sx - 8, 10))
            past_labels[name] = max(past_labels.get(name, 0), end + 8)
    for (h, opin), label in sorted(crossing.items()):
        if (h, opin) in from_label or (h, "out", opin) not in box:
            continue
        sx, sy = box[(h, "out", opin)]
        # Past the BOX, not just past the text: a wire leaving the same pin
        # started inside its own connector and read as crossing it.
        end = sx + 24 + len(label) * 3.4
        prims.append(line([(sx, sy), (sx + 8, sy)], arrow=True,
                          frm=_at(h, "out", opin)))
        prims.append(rect(sx + 12, sy - 4, len(label) * 3.4 + 8, 11, "", VAR_C))
        prims.append(text(sx + 16, sy + 2, label, 6.0, color=MUTED))
        from_label[(h, opin)] = (end, sy)
        blocking.append((sx + 8, sy - 5, end - sx - 8, 10))
        past_labels[h] = max(past_labels.get(h, 0), end + 8)

    def _out_at(head, opin):
        """Where a wire out of this pin starts: the row's label if it writes
        one, the pin itself if it does not."""
        return from_label.get((head, opin)) or box[(head, "out", opin)]

    # The wires. A source is either another block's output pin or a variable,
    # and a variable gets a stub on the left rail so the page names it once.
    left, lanes = [], {}
    for name, typ, ins, outs in spec:
        for pin, src in ins.items():
            if (name, "in", pin) not in box:
                continue
            tx, ty = box[(name, "in", pin)]
            body, srcs = _reads(name, src)
            drawn, crossed = False, None
            for h, opin in srcs:
                if (h, "out", opin) not in box:
                    opin = _pins(inst[h][1])[1][0]
                if (h, "out", opin) not in box:
                    unwired.append(f"{name}.{pin} <- {src}")
                    continue
                sx, sy = _out_at(h, opin)
                # A NAMED signal is a connector wherever a wire would have to
                # work for it: crossing rows, taking a detour, or merely being
                # MENTIONED in a formula - which already spells the name, so a
                # line drawn beside it says nothing the label has not.
                named = (h, opin) in pin_row
                mentioned = named and body != pin_row[(h, opin)]
                if (band_of.get(h) != band_of.get(name) or mentioned
                        or (named and not _straight(sx, sy, tx, ty,
                                                    past_labels.get(h, 0)))):
                    crossed = crossing.get((h, opin),
                                           pin_row.get((h, opin),
                                                       f"{h}.{opin}"))
                    drawn = True
                    continue
                prims.append(line(_route(sx, sy, tx, ty,
                                         bottom.get(name, height), lanes,
                                         past_labels.get(h, 0)),
                                  arrow=True, frm=_at(h, "out", opin),
                                  to=_at(name, "in", pin)))
                drawn = True
            # A wire says where it comes from; a FORMULA still has to say what
            # is done to it, so it keeps its label. A plain name does not - the
            # far end of the wire already carries it.
            bare = len(srcs) == 1 and body in (f"{srcs[0][0]}.{srcs[0][1]}",
                                               *[k for k, v in made.items()
                                                 if v == srcs[0]])
            # A formula already spells the connector's name, so it labels the
            # pin on its own; a bare source needs the name put there.
            if crossed is not None and bare:
                left.append((crossed, tx, ty, name, pin))
            elif not drawn or not bare:
                left.append((body, tx, ty, name, pin))

    # Beside ITS OWN block, never on one shared rail: a stub for a block two
    # columns in would otherwise be drawn at the far left, and its wire would
    # cross everything between.
    def _temp(label):
        """A label naming another node's pin rather than a row: a value this
        law holds for one beat and nothing outside it can see."""
        head = str(label).split(".")[0]
        return "." in str(label) and head in inst

    for label, tx, ty, node, pin in left:
        if _temp(label):
            # Boxed, because it is not a row: an operator cannot find this one
            # on any page, and it exists only between these two boxes.
            tw = len(label) * 3.4
            prims.append(rect(tx - 16 - tw, ty - 4, tw + 8, 11, "", VAR_C))
        prims.append(text(tx - 12, ty + 2, label, 6.0, "end", INK))
        prims.append(line([(tx - 6, ty), (tx, ty)], arrow=True,
                          to=_at(node, "in", pin)))
    # ...and the same on the way out: a stub at the far right would be drawn
    # straight through every column after this block's own.
    edge = xs[widest - 1] + bw
    deepest = max((f + 12 + 7 * 11 for f in lanes), default=height)
    return prims, int(edge + 120), int(max(height, deepest) + 14)




def _style(style, key, fallback=None):
    """One value out of an mxCell style string."""
    m = re.search(rf"(?:^|;){re.escape(key)}=([^;]*)", style or "")
    return m.group(1) if m else fallback


def _plain(html_value):
    """A cell's label as text: drawio keeps it as HTML, we draw strings."""
    v = re.sub(r"<br\s*/?>", "\n", html_value or "")
    v = re.sub(r"<[^>]+>", "", v)
    return html.unescape(v)


def read_drawio(path):
    """A page somebody has REWORKED, read back as prims.

    These files carry the editor's own model beside the picture, so a diagram
    moved by hand can still reach the PDF: the report renders THAT layout for
    the page instead of overwriting it with its own. Only a file drawio has
    saved counts - it leaves a signature the generator never writes.
    """
    if not os.path.exists(path):
        return None
    src = open(path).read()
    if "editors other than draw.io" not in src:
        return None                        # still the generator's own
    m = re.search(r'content="([^"]*)"', src)
    size = re.search(r'<svg[^>]*width="([\d.]+)(?:px)?"[^>]*height="([\d.]+)(?:px)?"',
                     src)
    if m is None or size is None:
        return None
    root = ET.fromstring(html.unescape(m.group(1)))
    geo, boxes, edges, labels = {}, [], [], []
    for c in root.iter("mxCell"):
        g = c.find("mxGeometry")
        style = c.get("style") or ""
        if c.get("vertex") == "1" and g is not None:
            x, y = float(g.get("x", 0)), float(g.get("y", 0))
            w, h = float(g.get("width", 0)), float(g.get("height", 0))
            geo[c.get("id")] = (x, y, w, h)
            val = _plain(c.get("value"))
            if style.startswith("text;"):
                sz = float(_style(style, "fontSize", 8))
                al = _style(style, "align", "left")
                anc = {"center": "middle", "right": "end"}.get(al, "start")
                ax = {"middle": x + w / 2.0, "end": x + w}.get(anc, x)
                labels.append(text(ax, y + 0.75 * sz + 2, val, sz, anc,
                                   _style(style, "fontColor", INK),
                                   "fontStyle=1" in style))
            else:
                head, _, tail = val.partition("\n")
                boxes.append(rect(x, y, w, h, head,
                                  _style(style, "fillColor", PLAIN), sub=tail,
                                  dashed="dashed=1" in style))
        elif c.get("edge") == "1" and g is not None:
            edges.append((c, g, style))

    def _end(cid, style, kind):
        """Where an attached edge really meets its box - which is the geometry,
        not the point drawio left behind when the box was moved."""
        if cid not in geo:
            return None
        fx, fy = _style(style, kind + "X"), _style(style, kind + "Y")
        if fx is None or fy is None:
            return None
        x, y, w, h = geo[cid]
        return (x + float(fx) * w, y + float(fy) * h)

    wires = []
    for c, g, style in edges:
        pt = {p.get("as"): (float(p.get("x", 0)), float(p.get("y", 0)))
              for p in g.findall("mxPoint")}
        a = _end(c.get("source"), style, "exit") or pt.get("sourcePoint")
        b = _end(c.get("target"), style, "entry") or pt.get("targetPoint")
        if a is None or b is None:
            continue
        arr = g.find("Array")
        way = [(float(q.get("x", 0)), float(q.get("y", 0)))
               for q in (arr if arr is not None else [])]
        wires.append(line([a] + way + [b], arrow="endArrow=block" in style,
                          label=_plain(c.get("value")),
                          dashed="dashed=1" in style,
                          color=_style(style, "strokeColor", MUTED)))

    # Boxes, then the wires over them, then every label on top - which is the
    # order the generator draws in, so a reworked page reads the same way.
    prims = boxes + wires + labels
    # Sized from what is IN it, not from the editor's page: drawio saves the
    # page it was showing, and a diagram can reach past it.
    right = bottom = 0.0
    for q in prims:
        if q.kind == "line":
            right = max([right] + [x for x, _y in q.pts])
            bottom = max([bottom] + [y for _x, y in q.pts])
        elif q.kind == "rect":
            right, bottom = max(right, q.x + q.w), max(bottom, q.y + q.h)
        else:
            wide = len(q.s) * q.size * 0.6
            right = max(right, q.x + (0 if q.anchor == "end" else wide))
            bottom = max(bottom, q.y)
    return (prims, max(float(size.group(1)), right + 12),
            max(float(size.group(2)), bottom + 12))


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


def main(site="gh_mlb41"):
    from reportlab.lib.pagesizes import A4
    from reportlab.pdfgen import canvas as rl

    site_dir = os.path.join(ROOT, "src", "sites", site)
    progs = vars_programs.load(site_dir)
    rows = _rows_of(site_dir)
    vars_programs.expand(progs, rows, lambda _m: None)

    os.makedirs(IMAGES, exist_ok=True)
    out = os.path.join(ROOT, "docs", "generated", f"{site}_logic.pdf")
    c = rl.canvas.Canvas(out, pagesize=A4) if hasattr(rl, "canvas") else \
        rl.Canvas(out, pagesize=A4)

    pw, ph = A4
    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 22)
    c.drawString(40, ph - 120, f"{site}: what each POU does inside")
    c.setFillColor(MUTED)
    c.setFont("Helvetica", 10)
    for i, l in enumerate(_wrap(
            "One page per control program and one per function block. Both of "
            "a program's pages come from the table that declares it: what it "
            "is connected to, and the network it owns. A block's own page is "
            "authored, because what a Pid does inside is not in any table. "
            "Every diagram is also an editable images/logic/*.drawio.svg, and "
            "one reworked in drawio is kept and followed here.", 96)):
        c.drawString(40, ph - 150 - i * 13, l)
    c.setFont("Helvetica", 9)
    c.drawString(40, 60, f"{len(progs)} programs, "
                         f"{len(all_blocks(site_dir))} blocks. "
                         f"One-time report: scripts/logic_report.py")
    c.showPage()

    made, crossings, unwired, kept = 0, [], [], []
    for p in progs:
        prims, dw, dh = program_diagram(p, rows)
        notes = [p.get("desc", "")]
        notes += [" ".join(x.split()) for x in
                  (p.get("notes") or "").strip().split("\n\n") if x.strip()]
        hooks = ["run()"] + [h + "()" for h in p["hooks"]]
        sub = (f"{p['class']} - {p['task']}, {p['stage']} stage - "
               f"defines {', '.join(hooks)}")
        prims, dw, dh = _held(p["name"], prims, dw, dh, kept)
        page(c, p["name"], sub, prims, dw, dh, notes)
        made += 1

        wiring = net_spec(p)
        if wiring:
            fprims, fdw, fdh = fbd_diagram(wiring, unwired)
            _audit(fprims, p["name"] + " (fbd)", crossings)
            fprims, fdw, fdh = _held(p["name"] + "_fbd", fprims, fdw, fdh, kept)
            page(c, p["name"] + ": the network", sub, fprims, fdw, fdh, [])
            made += 1

    for name, where, what, spec in all_blocks(site_dir):
        prims, dw, dh = LAYOUT[spec["kind"]](spec)
        _audit(prims, name, crossings)
        prims, dw, dh = _held("block_" + name.split()[0].replace("<", "")
                              .replace(">", "").replace("/", ""),
                              prims, dw, dh, kept)
        page(c, name, f"{where} - {what}", prims, dw, dh, spec.get("notes", []))
        made += 1

    c.save()
    for msg in crossings:
        print("  WARNING " + msg)
    for msg in unwired:
        print("  WARNING a pin names an output that does not exist: " + msg)
    print(f"  {os.path.relpath(out, ROOT)}: written ({made} diagrams)")
    print(f"  {os.path.relpath(IMAGES, ROOT)}/: "
          f"{made - len(kept)} editable .drawio.svg")
    for name in kept:
        print(f"  {name}: reworked by hand - kept, and the PDF follows it")


def _svg(name, prims, w, h):
    with open(os.path.join(IMAGES, f"{name}.drawio.svg"), "w") as f:
        f.write(to_svg(prims, w, h, name))


def _held(name, prims, w, h, kept):
    """This page as it should be DRAWN, and whether the file is left alone.

    A file drawio has saved is someone's own work: the PDF takes their layout
    and the generator does not write over it.
    """
    reworked = read_drawio(os.path.join(IMAGES, f"{name}.drawio.svg"))
    if reworked is None:
        _svg(name, prims, w, h)
        return prims, w, h
    kept.append(name)
    return reworked


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--site", default="gh_mlb41",
                    help="the site to draw (default: gh_mlb41)")
    main(**vars(ap.parse_args()))
