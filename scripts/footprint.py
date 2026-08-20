#!/usr/bin/env python3
"""Measure a linked image's flash and static-RAM footprint, and fail past a ceiling.

The OPTA linker script cannot do this job. Its `.heap` is defined as whatever is
left of the RAM region ( `. = ORIGIN(RAM) + LENGTH(RAM) - 0x400` ), so growth in
`.bss` silently shrinks the heap instead of overflowing the region, and its
`ASSERT(__StackLimit >= __HeapLimit, ...)` cannot fire until `.bss` alone nearly
fills 512 KB. Flash has no ceiling in effect at all: `LENGTH` is computed from
CM4_BINARY_START, which sits in QSPI, so the region reads as ~1.4 GB.

So the budget is enforced here instead:

  flash  = .text + .data + .ARM.extab + .ARM.exidx   (what lands in the .bin)
  static = .data + .bss  + .uninitialized            (what is gone before main)
  heap   = .heap                                     (the remainder; headroom)

Stdlib only. Shells out to `size` and `nm` (the arm-none-eabi- ones for a target
image). Exits non-zero past --flash-max or --ram-max.
"""

import argparse
import json
import os
import subprocess
import sys

# Sections that land in the flashed image, and those that consume RAM before
# main() runs. `.data` is in both: it occupies flash for its initialiser AND RAM
# for its live copy.
FLASH_SECTIONS = (".text", ".data", ".ARM.extab", ".ARM.exidx", ".rodata")
STATIC_RAM_SECTIONS = (".data", ".bss", ".uninitialized")
HEAP_SECTION = ".heap"

# nm type letters: what a symbol costs. Lower case is local, upper is global.
RAM_TYPES = set("bBdD")
FLASH_TYPES = set("tTrR")


def run(cmd):
    """Capture a tool's stdout, or None when it is missing or fails."""
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.stdout


def sections(size_tool, elf):
    """Section name -> size, from `size -A`."""
    text = run([size_tool, "-A", elf])
    if text is None:
        return None
    out = {}
    for line in text.splitlines():
        parts = line.split()
        # "<name> <size> <addr>"; the header and totals lines have no numeric size
        if len(parts) < 2 or not parts[0].startswith("."):
            continue
        try:
            out[parts[0]] = int(parts[1])
        except ValueError:
            continue
    return out


def symbols(nm_tool, elf, want_types):
    """(size, name) for every symbol of the wanted types, largest first."""
    text = run([nm_tool, "-S", "-td", "-C", elf])
    if text is None:
        return []
    out = []
    for line in text.splitlines():
        parts = line.split(None, 3)
        # "<addr> <size> <type> <name>" - a symbol without a size has 3 fields
        if len(parts) < 4 or parts[2] not in want_types:
            continue
        try:
            out.append((int(parts[1]), parts[3]))
        except ValueError:
            continue
    out.sort(key=lambda e: -e[0])
    return out


def total(secs, names):
    return sum(secs.get(n, 0) for n in names)


def human(n):
    """Bytes with a KB hint, right-sized for a build log."""
    return "{:>9,} B ({:>7.1f} KB)".format(n, n / 1024.0)


def bar(used, limit, width=32):
    """A fixed-width meter, so a regression is visible without reading digits."""
    if limit <= 0:
        return ""
    filled = min(width, int(round(width * used / float(limit))))
    return "[" + "#" * filled + "." * (width - filled) + "]"


def report(label, used, limit, out):
    pct = (100.0 * used / limit) if limit else 0.0
    out.append("  {:<12} {}  {} {:5.1f}% of {:,} B".format(
        label, human(used), bar(used, limit), pct, limit))
    return limit and used > limit


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", help="the linked image to measure")
    ap.add_argument("--size", default="size", help="the `size` tool")
    ap.add_argument("--nm", default="nm", help="the `nm` tool")
    ap.add_argument("--flash-max", type=int, default=0,
                    help="flash ceiling in bytes; 0 disables the check")
    ap.add_argument("--ram-max", type=int, default=0,
                    help="static-RAM ceiling in bytes; 0 disables the check")
    ap.add_argument("--ram-region", type=int, default=0,
                    help="size of the RAM region, for the heap-headroom line")
    ap.add_argument("--top", type=int, default=8,
                    help="how many of the largest objects to name")
    ap.add_argument("--json", default="",
                    help="also write the numbers here, for CI to consume")
    a = ap.parse_args()

    if not os.path.exists(a.elf):
        sys.stderr.write("footprint: no such image: {}\n".format(a.elf))
        return 1

    secs = sections(a.size, a.elf)
    if secs is None:
        # A missing cross-toolchain must not break a build that otherwise linked.
        sys.stderr.write("footprint: {} unavailable - skipping the check\n".format(a.size))
        return 0

    flash = total(secs, FLASH_SECTIONS)
    static = total(secs, STATIC_RAM_SECTIONS)
    heap = secs.get(HEAP_SECTION, 0)

    out = ["", "Footprint: {}".format(os.path.basename(a.elf))]
    over_flash = report("flash", flash, a.flash_max, out)
    over_ram = report("static RAM", static, a.ram_max, out)
    if heap:
        # The figure that quietly shrinks: the linker hands the heap whatever
        # .bss did not take, so this is the real headroom number.
        out.append("  {:<12} {}  (what .bss left over)".format("heap", human(heap)))
    if a.ram_region:
        out.append("  {:<12} {}".format("RAM region", human(a.ram_region)))

    big_ram = symbols(a.nm, a.elf, RAM_TYPES)[:a.top]
    if big_ram:
        out.append("  largest in RAM:")
        for size, name in big_ram:
            out.append("    {:>9,} B  {}".format(size, name[:88]))
    big_flash = symbols(a.nm, a.elf, FLASH_TYPES)[:a.top]
    if big_flash:
        out.append("  largest in flash:")
        for size, name in big_flash:
            out.append("    {:>9,} B  {}".format(size, name[:88]))
    out.append("")
    sys.stdout.write("\n".join(out) + "\n")

    numbers = {"flash": flash, "static_ram": static, "heap": heap,
               "flash_max": a.flash_max, "ram_max": a.ram_max}
    sys.stdout.write("FOOTPRINT {}\n".format(json.dumps(numbers, sort_keys=True)))
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(numbers, fh, sort_keys=True, indent=2)

    rc = 0
    if over_flash:
        sys.stderr.write("footprint: FLASH over budget: {:,} > {:,} bytes\n"
                         .format(flash, a.flash_max))
        rc = 1
    if over_ram:
        sys.stderr.write("footprint: STATIC RAM over budget: {:,} > {:,} bytes\n"
                         "  every byte here is a byte the heap does not get\n"
                         .format(static, a.ram_max))
        rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
