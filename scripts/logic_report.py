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
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vars_programs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, "src", "sites", "gh_mlb41")
IMAGES = os.path.join(ROOT, "images", "logic")

# The one palette, so the PDF and the SVG cannot drift apart.
INK    = "#1a1a1a"
MUTED  = "#6b6b6b"
IN_C   = "#d8e6f3"   # a row this POU reads
OUT_C  = "#dbeedd"   # a row it produces
BLK_C  = "#f2e5cc"   # a function block it holds
VAR_C  = "#ece6f2"   # what it remembers
PLAIN  = "#ffffff"


class Prim:
    """A drawn thing: the whole vocabulary both renderers understand."""

    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)


def rect(x, y, w, h, label="", fill=PLAIN, sub="", dashed=False):
    return Prim("rect", x=x, y=y, w=w, h=h, label=label, fill=fill, sub=sub,
                dashed=dashed)


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
                mx = (p.pts[0][0] + p.pts[-1][0]) / 2.0
                my = (p.pts[0][1] + p.pts[-1][1]) / 2.0
                c.setFillColor(p.color)
                c.setFont("Helvetica", 6.2 * scale)
                c.drawCentredString(X(mx), Y(my - 2), p.label)


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
                mx = (p.pts[0][0] + p.pts[-1][0]) / 2.0
                my = (p.pts[0][1] + p.pts[-1][1]) / 2.0
                body.append(_svg_text(mx, my - 2, p.label, 6.2, "middle", p.color))
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

    prims = []
    colw, cx = 168, [8, 236, W - 176]
    tallest = max(len(ins), len(outs), len(held) + len(kept), 1)
    y0 = 26

    prims.append(text(cx[0], 16, "VAR_INPUT / VAR_IN_OUT", 7, bold=True))
    prims.append(text(cx[1] + colw / 2.0, 16, "what it holds", 7, "middle",
                      bold=True))
    prims.append(text(cx[2] + colw, 16, "VAR_OUTPUT", 7, "end", bold=True))

    for i, (member, sec, names) in enumerate(ins):
        y = y0 + i * (ROW_H + GAP)
        n = f"{member}[{len(names)}]" if len(names) > 1 else member
        prims.append(rect(cx[0], y, colw, ROW_H, n, IN_C,
                          sub=_row_label(names)))
    for i, (member, sec, names) in enumerate(outs):
        y = y0 + i * (ROW_H + GAP)
        n = f"{member}[{len(names)}]" if len(names) > 1 else member
        prims.append(rect(cx[2], y, colw, ROW_H, n,
                          OUT_C if sec == "out" else VAR_C,
                          sub=_row_label(names)))

    mid_h = max(1, len(held) + len(kept)) * (ROW_H + GAP)
    body_h = max(len(ins), len(outs)) * (ROW_H + GAP)
    my = y0 + max(0, (body_h - mid_h) / 2.0)
    prims.append(rect(cx[1] - 6, y0 - 6, colw + 12,
                      max(mid_h + 12, body_h), "", PLAIN, dashed=True))
    for i, (name, typ) in enumerate(held + kept):
        y = my + i * (ROW_H + GAP)
        prims.append(rect(cx[1], y, colw, ROW_H, name,
                          BLK_C if i < len(held) else VAR_C, sub=typ))

    span = max(body_h, mid_h) + y0
    prims.append(line([(cx[0] + colw + 4, span / 2.0),
                       (cx[1] - 10, span / 2.0)], arrow=True))
    prims.append(line([(cx[1] + colw + 10, span / 2.0),
                       (cx[2] - 4, span / 2.0)], arrow=True))
    return prims, W, int(span + 16)


def _row_label(names):
    if len(names) == 1:
        return names[0]
    head = os.path.commonprefix(names).rstrip("0123456789")
    return f"{head}* x{len(names)}" if head else f"{len(names)} rows"


def flow_diagram(spec):
    """Signal flow: columns of boxes, wired left to right."""
    cols = spec["cols"]
    colw, gapx, y0 = 130, 46, 30
    tall = max(len(c) for c in cols)
    h = y0 + tall * (ROW_H + 12) + 12
    prims, pos = [], {}
    for ci, col in enumerate(cols):
        x = 12 + ci * (colw + gapx)
        top = y0 + (tall - len(col)) * (ROW_H + 12) / 2.0
        for ri, (name, sub, fill) in enumerate(col):
            y = top + ri * (ROW_H + 12)
            prims.append(rect(x, y, colw, ROW_H, name, fill, sub=sub))
            pos[name] = (x, y, colw, ROW_H)
    for a, b, label in spec.get("wires", []):
        if a not in pos or b not in pos:
            continue
        ax, ay, aw, ah = pos[a]
        bx, by, bw, bh = pos[b]
        prims.append(line([(ax + aw + 2, ay + ah / 2.0),
                           (bx - 2, by + bh / 2.0)], arrow=True, label=label))
    return prims, 12 + len(cols) * (colw + gapx), int(h + 4)


def states_diagram(spec):
    """A state machine: the states around a ring, the transitions labelled."""
    import math
    nodes = spec["states"]
    r, cx, cy, bw, bh = 96, 250, 124, 96, 26
    prims, pos = [], {}
    for i, (name, sub) in enumerate(nodes):
        a = -math.pi / 2.0 + i * 2.0 * math.pi / len(nodes)
        x = cx + r * 1.55 * math.cos(a) - bw / 2.0
        y = cy + r * math.sin(a) - bh / 2.0
        prims.append(rect(x, y, bw, bh, name, spec.get("fill", BLK_C), sub=sub))
        pos[name] = (x + bw / 2.0, y + bh / 2.0)
    for a, b, label in spec["edges"]:
        if a not in pos or b not in pos:
            continue
        (x0, y0), (x1, y1) = pos[a], pos[b]
        dx, dy = x1 - x0, y1 - y0
        d = max(1.0, (dx * dx + dy * dy) ** 0.5)
        k = bw / 2.2
        prims.append(line([(x0 + dx / d * k, y0 + dy / d * k),
                           (x1 - dx / d * k, y1 - dy / d * k)],
                          arrow=True, label=label))
    return prims, 500, int(cy + r + bh + 6)


def curve_diagram(spec):
    """A transfer curve: what comes out for what goes in."""
    x0, y0, w, h = 46, 28, 330, 120
    prims = [line([(x0, y0), (x0, y0 + h), (x0 + w, y0 + h)]),
             text(x0 - 6, y0 + 6, spec.get("ylab", "out"), 6.6, "end"),
             text(x0 + w, y0 + h + 12, spec.get("xlab", "in"), 6.6, "end")]
    pts = [(x0 + px * w, y0 + h - py * h) for px, py in spec["points"]]
    prims.append(line(pts, color=INK))
    for px, label in spec.get("marks", []):
        prims.append(line([(x0 + px * w, y0), (x0 + px * w, y0 + h)],
                          dashed=True))
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
   "edges": [("q = false", "q = true", "Sr: set    Rs: set && !reset"),
             ("q = true", "q = false", "Sr: reset && !set    Rs: reset")],
   "notes": ["Sr is SET-dominant: q = set || (q && !reset). Both true, and it "
             "latches.",
             "Rs is RESET-dominant: q = !reset && (set || q). Both true, and it "
             "drops. Pick the one whose dominant input is the safe one - a "
             "lockout that must clear on recovery is an Rs."]}),

 ("Hysteresis", "analog.h", "a band with two edges", {
   "kind": "curve",
   "points": [(0.0, 0.05), (0.55, 0.05), (0.55, 0.9), (1.0, 0.9)],
   "marks": [(0.3, "low"), (0.55, "high")],
   "xlab": "value", "ylab": "q",
   "notes": ["q goes true at high and false at low, and NOTHING happens in "
             "between: a reading sitting on one threshold cannot chatter.",
             "The curve is drawn rising; going back down it falls at low, not "
             "at high.",
             "Used for: the intake gates (cooling), the dryness gate "
             "(humidity)."]}),

 ("Hysteresis3", "analog.h", "three bands, one reading", {
   "kind": "curve",
   "points": [(0.0, 0.05), (0.3, 0.05), (0.3, 0.45), (0.62, 0.45),
              (0.62, 0.85), (1.0, 0.85)],
   "marks": [(0.3, "edge 1"), (0.62, "edge 2")],
   "xlab": "value", "ylab": "level",
   "notes": ["Two thresholds, each with its own hysteresis, giving three "
             "levels. A level change needs the reading to cross a whole band."]}),

 ("Staircase<N>", "analog.h", "N rungs, each hysteretic", {
   "kind": "curve",
   "points": [(0.0, 0.03), (0.22, 0.03), (0.22, 0.35), (0.5, 0.35),
              (0.5, 0.66), (0.78, 0.66), (0.78, 0.97), (1.0, 0.97)],
   "marks": [(0.22, "edge[0]"), (0.5, "edge[1]"), (0.78, "edge[2]")],
   "xlab": "value", "ylab": "level 0..N",
   "notes": ["One Hysteresis per rung, each engaging at edge[i] and releasing "
             "at edge[i] - hyst. Returns how many rungs the reading is above.",
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
   "marks": [(0.28, "+band"), (0.66, "+band")],
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
            [("on_ms", "duty * period", BLK_C),
             ("floor", "under min_on -> 0", BLK_C),
             ("phase", "phase += dt, wraps", BLK_C)],
            [("coil", "phase < on_ms", OUT_C)]],
   "wires": [("duty", "on_ms", ""), ("period_ms", "phase", ""),
             ("min_on_ms", "floor", ""), ("on_ms", "floor", ""),
             ("floor", "coil", ""), ("phase", "coil", "")],
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
   "cols": [[("V, A, W, Hz", "four field reads", IN_C)],
            [("deadband x4", "one per quantity", BLK_C)],
            [("in_range", "V and Hz against their bands", BLK_C)],
            [("status rows", "the mirror", OUT_C), ("ok", "", OUT_C),
             ("fault", "out of spec, or gone silent", OUT_C)]],
   "wires": [("V, A, W, Hz", "deadband x4", ""),
             ("deadband x4", "status rows", ""),
             ("V, A, W, Hz", "in_range", ""), ("in_range", "ok", ""),
             ("in_range", "fault", "")],
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
            "program's diagram is DERIVED from the table that declares it, so "
            "it cannot go stale. A block's is authored from its source, "
            "because what a Pid does inside is not in any table. Every diagram "
            "is also an editable images/logic/*.drawio.svg, drawn from the "
            "same model as the page.", 96)):
        c.drawString(40, ph - 150 - i * 13, l)
    c.setFont("Helvetica", 9)
    c.drawString(40, 60, f"{len(progs)} programs, {len(BLOCKS)} blocks. "
                         f"One-time report: scripts/logic_report.py")
    c.showPage()

    made = 0
    for p in progs:
        prims, dw, dh = program_diagram(p, rows)
        notes = [p.get("desc", "")]
        notes += [" ".join(x.split()) for x in
                  (p.get("notes") or "").strip().split("\n\n") if x.strip()]
        if p["fns"]:
            notes.append("Its own helpers: " +
                         ", ".join(f["name"] + "()" for f in p["fns"]) + ".")
        hooks = ["run()"] + [h + "()" for h in p["hooks"]]
        page(c, p["name"], f"{p['class']} - {p['task']}, {p['stage']} stage - "
                           f"defines {', '.join(hooks)}",
             prims, dw, dh, notes)
        _svg(f"{p['name']}", prims, dw, dh)
        made += 1

    for name, where, what, spec in BLOCKS:
        prims, dw, dh = LAYOUT[spec["kind"]](spec)
        page(c, name, f"{where} - {what}", prims, dw, dh, spec.get("notes", []))
        _svg("block_" + name.split()[0].replace("<", "").replace(">", "")
             .replace("/", ""), prims, dw, dh)
        made += 1

    c.save()
    print(f"  {os.path.relpath(out, ROOT)}: written ({made} diagrams)")
    print(f"  {os.path.relpath(IMAGES, ROOT)}/: {made} editable .drawio.svg")


def _svg(name, prims, w, h):
    with open(os.path.join(IMAGES, f"{name}.drawio.svg"), "w") as f:
        f.write(to_svg(prims, w, h, name))


if __name__ == "__main__":
    main()
