#!/usr/bin/env python3
"""Generate docs/generated/scan-cycle.pdf - the scan cycle design note.

A standalone report: what the beat is, how a component hands over its work, and
how a site writes control programs. Regenerate after changing the cycle:

    python3 scripts/scan_cycle_report.py

Everything factual here is quoted from the tree; the header stamps the commit it
was generated from, so a stale report is visible rather than merely wrong.
"""

import subprocess
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, KeepTogether,
                                NextPageTemplate, PageBreak, PageTemplate,
                                Paragraph, Preformatted, Spacer, Table,
                                TableStyle)

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "generated" / "scan-cycle.pdf"

# Palette: the deep green the drawio diagrams already use, plus greys.
INK = colors.HexColor("#1B1F1D")
ACCENT = colors.HexColor("#14655A")
MUTED = colors.HexColor("#6B7671")
RULE = colors.HexColor("#C9D2CC")
WASH = colors.HexColor("#F2F5F2")
CODEBG = colors.HexColor("#F6F7F3")
WARNBG = colors.HexColor("#FBF3E7")
WARNRULE = colors.HexColor("#C88A2E")

MARGIN = 20 * mm
PAGE_W, PAGE_H = A4
BODY_W = PAGE_W - 2 * MARGIN


# --------------------------------------------------------------------------
# styles
# --------------------------------------------------------------------------

def styles():
    base = dict(fontName="Helvetica", textColor=INK, alignment=TA_LEFT)
    s = {}
    s["title"] = ParagraphStyle("title", fontName="Helvetica-Bold", fontSize=23,
                                leading=27, textColor=ACCENT, spaceAfter=2)
    s["subtitle"] = ParagraphStyle("subtitle", fontName="Helvetica", fontSize=11,
                                   leading=15, textColor=MUTED, spaceAfter=14)
    s["h1"] = ParagraphStyle("h1", fontName="Helvetica-Bold", fontSize=14.5,
                             leading=18, textColor=ACCENT, spaceBefore=16,
                             spaceAfter=6, keepWithNext=1)
    s["h2"] = ParagraphStyle("h2", fontName="Helvetica-Bold", fontSize=11,
                             leading=14, textColor=INK, spaceBefore=11,
                             spaceAfter=4, keepWithNext=1)
    s["body"] = ParagraphStyle("body", fontSize=9.6, leading=14.2,
                               spaceAfter=6, **base)
    s["bullet"] = ParagraphStyle("bullet", parent=s["body"], leftIndent=11,
                                 bulletIndent=2, spaceAfter=3)
    s["caption"] = ParagraphStyle("caption", fontName="Helvetica-Oblique",
                                  fontSize=8.2, leading=11, textColor=MUTED,
                                  spaceBefore=3, spaceAfter=10)
    s["code"] = ParagraphStyle("code", fontName="Courier", fontSize=7.6,
                               leading=10.2, textColor=INK, backColor=CODEBG,
                               borderPadding=(6, 7, 6, 7), spaceBefore=3,
                               spaceAfter=9, leftIndent=1)
    s["cell"] = ParagraphStyle("cell", fontSize=8.4, leading=11.4, **base)
    s["cellb"] = ParagraphStyle("cellb", parent=s["cell"],
                                fontName="Helvetica-Bold")
    s["cellc"] = ParagraphStyle("cellc", parent=s["cell"], fontName="Courier",
                                fontSize=7.8)
    s["note"] = ParagraphStyle("note", fontSize=9.2, leading=13.4,
                               textColor=INK, spaceAfter=5)
    s["notehead"] = ParagraphStyle("notehead", fontName="Helvetica-Bold",
                                   fontSize=9.2, leading=13.4,
                                   textColor=colors.HexColor("#8A5A12"),
                                   spaceAfter=3)
    return s


S = styles()


def para(t, st="body"):
    return Paragraph(t, S[st])


def bullets(items):
    return [Paragraph(f"&bull;&nbsp;&nbsp;{t}", S["bullet"]) for t in items]


def code(text, source=None):
    out = [Preformatted(text.strip("\n"), S["code"])]
    if source:
        out.append(Paragraph(source, S["caption"]))
    return KeepTogether(out)


def table(rows, widths, head=True):
    data = []
    for i, row in enumerate(rows):
        cells = []
        for cell in row:
            if isinstance(cell, str):
                st = "cellb" if (head and i == 0) else "cell"
                if cell.startswith("`") and cell.endswith("`"):
                    cell, st = cell[1:-1], "cellc"
                cells.append(Paragraph(cell, S[st]))
            else:
                cells.append(cell)
        data.append(cells)
    t = Table(data, colWidths=widths, hAlign="LEFT")
    style = [
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("LINEBELOW", (0, 0), (-1, -2), 0.4, RULE),
    ]
    if head:
        style += [("BACKGROUND", (0, 0), (-1, 0), WASH),
                  ("LINEBELOW", (0, 0), (-1, 0), 0.9, ACCENT)]
    t.setStyle(TableStyle(style))
    return t


def callout(title, body_paras):
    inner = [Paragraph(title, S["notehead"])]
    inner += [Paragraph(b, S["note"]) for b in body_paras]
    t = Table([[inner]], colWidths=[BODY_W], hAlign="LEFT")
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), WARNBG),
        ("LINEBEFORE", (0, 0), (0, -1), 2.2, WARNRULE),
        ("LEFTPADDING", (0, 0), (-1, -1), 9),
        ("RIGHTPADDING", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 7),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
    ]))
    return KeepTogether([t, Spacer(1, 9)])


# --------------------------------------------------------------------------
# the beat diagram
# --------------------------------------------------------------------------

PHASES = [
    ("INPUT", ["modbus_rtu_rx", "modbus_tcp_rx", "expire_stale", "console_in"]),
    ("CONFIG", ["params", "serial_cfg"]),
    ("SERVICE", ["health", "alarms", "control_latch", "net / ntp / mqtt"]),
    ("CONTROL", ["supply_watch", "supply_report", "site_control"]),
    ("OUTPUT", ["modbus_rtu_tx"]),
    ("PRESENT", ["logger", "console", "console_tx"]),
]


class Beat(Flowable):
    """One beat, left to right: six phases then the watchdog kick.

    CONTROL is drawn dashed - it is the only phase whose entries are gated by
    control authority, and the gate reaches the programs inside it, not the
    phases around it.
    """

    def __init__(self, width):
        Flowable.__init__(self)
        self.width = width
        self.height = 62 * mm

    def draw(self):
        c = self.canv
        n = len(PHASES)
        gap = 2.0 * mm
        kick_w = 15 * mm
        w = (self.width - kick_w - gap * n) / n
        top = self.height - 9 * mm
        box_h = 7.5 * mm

        # the time arrow
        c.setStrokeColor(RULE)
        c.setLineWidth(1.1)
        y = self.height - 3 * mm
        c.line(0, y, self.width - 2 * mm, y)
        c.setFillColor(RULE)
        c.saveState()
        p = c.beginPath()
        p.moveTo(self.width, y)
        p.lineTo(self.width - 3 * mm, y + 1.4 * mm)
        p.lineTo(self.width - 3 * mm, y - 1.4 * mm)
        p.close()
        c.drawPath(p, fill=1, stroke=0)
        c.restoreState()
        c.setFillColor(MUTED)
        c.setFont("Helvetica-Oblique", 7)
        c.drawString(0, y + 1.6 * mm, "tick - one beat, 10 ms")

        for i, (name, tasks) in enumerate(PHASES):
            x = i * (w + gap)
            gated = name == "CONTROL"
            c.setFillColor(ACCENT if not gated else colors.HexColor("#1F7D6F"))
            c.setStrokeColor(colors.white)
            c.roundRect(x, top - box_h, w, box_h, 1.2 * mm, fill=1, stroke=0)
            c.setFillColor(colors.white)
            c.setFont("Helvetica-Bold", 7.4)
            c.drawCentredString(x + w / 2, top - box_h + 2.5 * mm, name)

            # the tasks that sit in this phase
            c.setFillColor(INK)
            c.setFont("Courier", 6.1)
            ty = top - box_h - 4.2 * mm
            for t in tasks:
                c.drawCentredString(x + w / 2, ty, t)
                ty -= 3.3 * mm

            if gated:
                c.setStrokeColor(WARNRULE)
                c.setLineWidth(0.9)
                c.setDash(2.2, 1.8)
                pad = 1.3 * mm
                h = box_h + 4.0 * mm + 3.3 * mm * len(tasks)
                c.roundRect(x - pad, top - h - pad, w + 2 * pad,
                            h + 2 * pad, 1.4 * mm, fill=0, stroke=1)
                c.setDash()
                c.setFillColor(WARNRULE)
                c.setFont("Helvetica-Oblique", 6.3)
                c.drawCentredString(x + w / 2, top - h - 4.6 * mm,
                                    "control authority")

        # the watchdog kick
        x = n * (w + gap)
        c.setFillColor(MUTED)
        c.roundRect(x, top - box_h, kick_w, box_h, 1.2 * mm, fill=1, stroke=0)
        c.setFillColor(colors.white)
        c.setFont("Helvetica-Bold", 6.4)
        c.drawCentredString(x + kick_w / 2, top - box_h + 2.5 * mm, "watchdog")
        c.setFillColor(INK)
        c.setFont("Courier", 6.1)
        c.drawCentredString(x + kick_w / 2, top - box_h - 4.2 * mm, "if healthy")


# --------------------------------------------------------------------------
# page furniture
# --------------------------------------------------------------------------

def git_stamp():
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "log", "-1", "--format=%h, %cd",
             "--date=short"], text=True).strip()
    except Exception:
        return "unknown"


STAMP = git_stamp()


def furniture(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.5)
    canvas.line(MARGIN, MARGIN - 5 * mm, PAGE_W - MARGIN, MARGIN - 5 * mm)
    canvas.setFont("Helvetica", 7.4)
    canvas.setFillColor(MUTED)
    canvas.drawString(MARGIN, MARGIN - 9 * mm,
                      f"Botanik / libbotanik - the scan cycle ({STAMP})")
    canvas.drawRightString(PAGE_W - MARGIN, MARGIN - 9 * mm,
                           f"{canvas.getPageNumber()}")
    canvas.restoreState()


# --------------------------------------------------------------------------
# content
# --------------------------------------------------------------------------

def story():
    f = []
    f.append(para("The scan cycle", "title"))
    f.append(para(
        "One dispatcher, one table, and what a site plugs into it.<br/>"
        f"Generated from {STAMP}.", "subtitle"))

    f.append(para(
        "Everything the firmware does in a beat is a <b>task</b>: a name, a rate "
        "group, a phase, and one handler. <font face='Courier' size='9'>"
        "LoopScheduler</font> paces the beat and runs it, "
        "<font face='Courier' size='9'>Tasks::Registry</font> holds the table, "
        "and every component hands over its own entries. No object steps "
        "another, and there is no list of calls anywhere that has to be kept in "
        "the right order by hand."))

    # -- 1. the beat ------------------------------------------------------
    f.append(para("1. The beat", "h1"))
    f.append(para(
        "A beat is six <b>phases</b> in a fixed order, then the watchdog kick. "
        "The order is the data flow, so it is written out as code and is not "
        "configurable: inputs are refreshed before anything reads them, control "
        "runs after config and health, and outputs are handed to the field "
        "after control has decided."))
    f.append(Beat(BODY_W))
    f.append(para(
        "One beat. A task runs only on the beats its rate group has come due, so "
        "most beats are far emptier than this. Only CONTROL is gated by control "
        "authority.", "caption"))

    f.append(para(
        "Within a phase, tasks run in <b>registration order</b>. That is the "
        "whole ordering rule, and it is what makes a site's program table an "
        "execution order rather than a set."))

    f.append(para("Two axes, deliberately separate", "h2"))
    f.append(para(
        "<b>Phase</b> is where in the beat a task sits: a correctness property, "
        "because it encodes what must happen before what. <b>Rate</b> is its "
        "group: a tuning knob. Neither derives from the other - the field-bus "
        "client is fast-rate and split across two phases, the expiry sweep is "
        "background-rate and input-phase. A group must never imply an order, or "
        "retuning a rate would silently reorder the cycle."))
    f.append(table([
        ["Group", "Period", "What is on it"],
        ["`fast`", "10 ms", "the field bus, the log drain, the serial pumps"],
        ["`main`", "50 ms", "the operator-facing rate"],
        ["`slow`", "100 ms", "dwell and config: health, parameters, the console draw"],
        ["`background`", "500 ms", "housekeeping measured in seconds: the expiry sweep"],
    ], [26 * mm, 20 * mm, BODY_W - 46 * mm]))
    f.append(Spacer(1, 4))
    f.append(para(
        "There is deliberately <b>no priority</b>. Priority means preemption and "
        "there is none here: one cooperative loop, every task runs to "
        "completion. The only other reading would be load-shedding, and shedding "
        "a control program is not something that should ever happen "
        "automatically."))

    # -- 2. the table -----------------------------------------------------
    f.append(para("2. The table", "h1"))
    f.append(para(
        "One fixed-capacity array, no allocation, frozen once the loop starts - "
        "which is what lets the TASKS page hold a stable row order across "
        "redraws. Programs, components and setup handlers are all rows in it."))
    f.append(code("""
struct Task {
    const char* name;                                  // the TASKS row
    GroupId     group;                                 // how often
    Phase       phase;                                 // where in the beat
    Component*  owner;                                 // the erased self
    void      (*handler)(Component*, const Tick&);     // the call
    bool        is_program;                            // control-gated
    bool        first_activation;                      // -> Tick::first
    TaskTimer   timer;                                 // due / late / dropped
    TaskCost    cost;                                  // what it costs
};
""", "src/libbotanik/tasks/tasks.h (the component form; see §6)"))

    f.append(para(
        "Dispatch is <b>phase-major</b>: for each phase in order, walk the table "
        "and run every entry in that phase whose group has come due, timing each "
        "into its own <font face='Courier' size='9'>TaskCost</font>. A handler is "
        "told about the activation it is running in:"))
    f.append(table([
        ["`Tick` field", "Means"],
        ["`now_ms`", "the beat timestamp - the same for every task this beat"],
        ["`dt_ms`", "the <b>actual</b> ms since this task last ran, never its "
                    "nominal period. Anything integrating over time must use it"],
        ["`missed`", "activations dropped on the way here. A late task runs "
                     "<b>once</b>; missed activations are never caught up, "
                     "because a catch-up burst arrives exactly when the system "
                     "is already behind"],
        ["`first`", "this is the task's first activation since it last had "
                    "nothing to go on: at bring-up, and for a program when "
                    "control authority returns"],
    ], [24 * mm, BODY_W - 24 * mm]))

    f.append(para("Offsets are derived, not declared", "h2"))
    f.append(para(
        "Every timer seeds itself on its first run and everything runs on the "
        "first beat, so unoffset periods would stay phase-locked to boot for "
        "ever: rates would cut the <i>mean</i> work per beat and leave the "
        "<i>peak</i> exactly where it was. Registration counts how many members "
        "a group already has and places each new one a beat further along, "
        "wrapped into the group's period. A site writes nothing, and adding a "
        "task re-spreads its group."))

    f.append(PageBreak())

    # -- 3. registering ---------------------------------------------------
    f.append(para("3. How a component registers", "h1"))
    f.append(para(
        "A component hands over its own tasks, naming each one's phase and rate "
        "<i>itself</i>, beside the code they refer to. The composition root says "
        "only <i>which instance</i> - which is the one thing it knows and the "
        "component does not, since the same class may be constructed more than "
        "once."))
    f.append(code("""
// console.h  - public, because an override inherits the access of the
//              section it is declared in
void register_tasks(Tasks::Registry& r) override;

// console.cpp - the handlers, beside the methods they call
void Console::_input_task(Component* c, const Tasks::Tick&) {
    static_cast<Console*>(c)->_take_input();
}
void Console::_draw_task (Component* c, const Tasks::Tick& t) {
    static_cast<Console*>(c)->_draw(t.now_ms);
}
void Console::_flush_task(Component* c, const Tasks::Tick&) {
    static_cast<Console*>(c)->_flush();
}

void Console::register_tasks(Tasks::Registry& r) {
    r.add(*this, &Console::_input_task, "console_in", GROUP_FAST, PHASE_INPUT);
    r.add(*this, &Console::_draw_task,  "console",    GROUP_SLOW, PHASE_PRESENT);
    r.add(*this, &Console::_flush_task, "console_tx", GROUP_FAST, PHASE_PRESENT);
}
""", "src/libbotanik/console/console.cpp - one component, three tasks, two rates"))

    f.append(para("Why a named static, and not a lambda or a member pointer", "h2"))
    f.append(*[]) if False else None
    f += bullets([
        "<font face='Courier' size='9'>void (Console::*)(const Tick&amp;)</font> "
        "does not convert to "
        "<font face='Courier' size='9'>void (Component::*)(const Tick&amp;)</font> "
        "- pointer-to-member converts base to derived, not derived to base - so "
        "a member pointer cannot be type-erased into the table without a "
        "template.",
        "A <b>named</b> wrapper is greppable, takes a breakpoint, and appears in "
        "a stack trace. A lambda buried in a registration list is none of those.",
        "A <b>static member</b> rather than a file-local function when the hook "
        "it calls is private or protected: a member reaches it without a friend "
        "declaration, and the cast stays on one line beside the class it casts "
        "to.",
    ])
    f.append(Spacer(1, 5))
    f.append(para(
        "Phase-named virtual hooks were the alternative and do not work: they "
        "allow only one task per phase, and <font face='Courier' size='9'>"
        "Console</font> needs two in PRESENT at different rates. They also make "
        "the component restate its phase, which is the mismatch this design "
        "exists to remove - previously the composition root picked a phase and "
        "the component named it again by which hook it overrode, and nothing "
        "checked the two agreed."))

    f.append(para("4. A registration is more than the hook", "h1"))
    f.append(para(
        "Registering is also what gets a component <b>set up</b> and what keeps "
        "its <b>configuration</b> current. Both are the registry's job, because "
        "a handler dispatches one method and nothing else."))
    f.append(table([
        ["Before the loop", "Every beat"],
        ["<font face='Courier' size='9'>setup()</font> runs "
         "<font face='Courier' size='9'>on_setup()</font> then "
         "<font face='Courier' size='9'>announce()</font>, once, however many "
         "phases the component registered in. Late in bring-up, after every log "
         "sink exists - it is what runs the health monitor's POST - and out of "
         "the first beat, since the parameter load is tens of ms of flash.",
         "<font face='Courier' size='9'>poll_config()</font> re-reads the "
         "watched config group's commit serial and dispatches "
         "<font face='Courier' size='9'>on_param_changed()</font> when it has "
         "moved. Once per beat per owner, before the hook that reads the "
         "config."],
    ], [BODY_W / 2, BODY_W / 2]))
    f.append(Spacer(1, 6))
    f.append(callout(
        "Learned the hard way, twice",
        ["A missing registration is <b>invisible</b>: a refused or skipped "
         "<font face='Courier' size='9'>add()</font> does not fail the build and "
         "does not fail a test. The work simply never runs.",
         "Worse, so is a <i>partial</i> one. When components moved off being "
         "stepped and onto registering themselves, the handler called the phase "
         "hook directly and everything the old "
         "<font face='Courier' size='9'>step()</font> did around that hook "
         "stopped happening - silently. The health monitor's POST never ran, so "
         "all six platform-health alarms stayed unbound and could never be "
         "raised. And no component could reconfigure, which left the network "
         "handler waiting in <font face='Courier' size='9'>NET_IDLE</font> for a "
         "callback nothing made: the network never came up at all.",
         "So: when a component stops being stepped, walk the old "
         "<font face='Courier' size='9'>step()</font> line by line and ask who "
         "does each line now. And check the boot log, not just the task list - a "
         "task that runs having never been set up looks perfectly registered."]))

    f.append(PageBreak())

    # -- 5. control authority + programs ----------------------------------
    f.append(para("5. Control authority", "h1"))
    f.append(para(
        "Whether the firmware may drive the field is decided by the variable "
        "bank, and it gates <b>program entries and nothing else</b>. The phases "
        "around CONTROL must not be gated with it: withdrawing control writes "
        "safe states into the image, and something still has to carry them out "
        "to the field."))
    f.append(code("""
void Registry::set_programs_enabled(bool enabled, uint64_t now_ms);
""", "src/libbotanik/tasks/tasks.h - the whole edge, in one idempotent call"))
    f.append(table([
        ["Edge", "What happens", "Why"],
        ["withdrawn", "the gate shuts, and nothing else",
         "a program that is not called cannot act on what it remembers, and "
         "nothing outside it reads that"],
        ["returned", "the gate opens, every program's <b>timer is reseeded</b>, "
                     "and <font face='Courier' size='9'>Tick::first</font> is "
                     "raised on each",
         "a <font face='Courier' size='9'>Ton</font> holding 5 s handed an hour "
         "of <font face='Courier' size='9'>dt</font> would fire the instant "
         "control returned - the one thing a withdrawal exists to prevent. The "
         "flag is the other half: it says the process moved without us, which "
         "<font face='Courier' size='9'>dt</font> alone cannot express"],
    ], [22 * mm, 52 * mm, BODY_W - 74 * mm]))
    f.append(Spacer(1, 5))
    f.append(para(
        "A program therefore has <b>no control-authority hooks</b> - no "
        "<font face='Courier' size='9'>reset()</font>, no "
        "<font face='Courier' size='9'>resume()</font>. "
        "<font face='Courier' size='9'>first_scan</font> is the whole signal, "
        "and it is better placed than a hook on the edge would be: because the "
        "timer is reseeded, a program does not run on the beat authority "
        "returned, so an edge hook would look at an image its own next "
        "<font face='Courier' size='9'>run()</font> never sees. Bring-up needs "
        "no special case either - a task's first activation ever raises the same "
        "flag, and a program knows no more about the plant at bring-up than "
        "after an outage."))

    # -- 6. a site's programs --------------------------------------------
    f.append(para("6. A site's own programs", "h1"))
    f.append(para(
        "A control program is a <font face='Courier' size='9'>Plc::Program</font> "
        "subclass. It holds its function blocks and its resolved variable "
        "references as members, and overrides two methods. It registers itself "
        "like any other component - the difference is that it may only register "
        "in CONTROL, and its entry is the one the authority gate can shut."))
    f.append(table([
        ["Hook", "When", "For"],
        ["`bind(io)`", "once, at bring-up",
         "names into <font face='Courier' size='9'>VarRef</font>s, so "
         "<font face='Courier' size='9'>run()</font> never searches the table"],
        ["`run(c)`", "each activation", "read, apply blocks, drive"],
    ], [26 * mm, 34 * mm, BODY_W - 60 * mm]))
    f.append(Spacer(1, 7))
    f.append(code("""
class SupplyWatch : public Plc::Program {
    Plc::Ton     _bad_for;                       // debounce
    Plc::Sr      _lockout;
    Vars::VarRef _ln = { Vars::VAR_NONE }, _locked = { Vars::VAR_NONE };

public:
    SupplyWatch() : Program("supply_watch", Tasks::GROUP_SLOW) {}

protected:
    void bind(Vars::Bank& io) override {
        _ln     = io.find("grid_LN_V");
        _locked = io.find("sys_supply_lockout");
    }

    void run(Plc::Context& c) override {
        // Both blocks are a claim about the supply over TIME, and not being
        // called means we stopped watching it.
        if (c.first_scan) { _bad_for.reset(); _lockout.reset(); }

        const bool unhealthy = !Plc::in_range(c.io.value(_ln), 200.0f, 260.0f);
        _lockout(_bad_for(unhealthy, 5000, c.dt_ms), !unhealthy);
        c.io.set(_locked, _lockout.q());
    }
};

// The site's table: pointers, so it stays const and flash-resident even
// though what it points at is RAM. Order IS execution order.
Plc::Program* const g_programs[] = {
    &Plc::Instance<SupplyWatch>::obj,
    &Plc::Instance<SupplyReport>::obj,
};
""", "src/sites/gh_mlb41/tasks.h - the site declares programs, the base registers them"))

    f.append(para("What the scaffolding guarantees", "h2"))
    f += bullets([
        "<b>Table order is execution order.</b> The firmware registers programs "
        "in table order and a phase dispatches in registration order, so a law "
        "that computes a setpoint and a law that acts on it complete in one beat "
        "when written that way round, and take a beat per link written the other "
        "way.",
        "<b>The image is coherent within a beat.</b> Inputs refresh once, before "
        "any program runs, so every program in a beat sees the same instant. "
        "There are no per-program snapshots: programs run sequentially in one "
        "thread and never span a beat.",
        "<b>Staleness is explicit.</b> A row past its timeout goes invalid, and "
        "an invalid row reads as its declared default - so a program handles a "
        "dead sensor correctly without ever testing "
        "<font face='Courier' size='9'>valid()</font>.",
        "<b>A rate group, not a period.</b> A program is scheduled like "
        "everything else in the beat, so retuning a rate class retunes it too.",
    ])
    f.append(Spacer(1, 4))
    f.append(para(
        "Note what the example does <i>not</i> do: no clock reads, no direct "
        "I/O, no knowledge of Modbus, and no name lookup once it is running. It "
        "reads variables, applies blocks, and drives variables."))

    f.append(para("7. Verifying a registration", "h1"))
    f.append(para(
        "Ask the shell, never the TASKS page. "
        "<font face='Courier' size='9'>tasks get &lt;name&gt;</font> answers "
        "\"no task named\" or prints the detail, and that is decisive; the page "
        "is a name-sorted scrolling table, so a scroll that stops short reads "
        "exactly like an absent task."))
    f.append(code("""
botanik> tasks get supply_watch
Runs in .............. : control (slow, every 100 ms)
Activations .......... : 105 run, 0 missed, last 0 ms ago
Cost ................. : last 0 us, mean 0 us, peak 0 us
Duty ................. : 0.00 % of its own period
""", "and `tasks` alone for the totals: registered, refused, missed, the costliest"))
    f.append(para(
        "Build <b>both</b> targets before believing it. The simulator's include "
        "order has hidden a missing forward declaration that failed only on the "
        "OPTA one."))

    return [x for x in f if x is not None]


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    doc = BaseDocTemplate(str(OUT), pagesize=A4,
                          leftMargin=MARGIN, rightMargin=MARGIN,
                          topMargin=MARGIN, bottomMargin=MARGIN + 4 * mm,
                          title="Botanik - the scan cycle",
                          author="libbotanik", subject="firmware design note")
    frame = Frame(MARGIN, MARGIN + 4 * mm, BODY_W,
                  PAGE_H - MARGIN - (MARGIN + 4 * mm), id="body")
    doc.addPageTemplates([PageTemplate(id="p", frames=[frame],
                                       onPage=furniture)])
    doc.build(story())
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
