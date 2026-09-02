# Flash footprint {#dev_footprint}

What the OPTA image is made of, which parts of it are ours, and the two levers
that move it. Measured off `botanik_opta.map` and the linked ELF, not counted
from the source.

The flash budget is 786,432 bytes. Numbers below are `-Os`, the reference site,
every feature on.

## Where the image goes

| Origin | Bytes | Ours? |
|---|---|---|
| our code (libbotanik + site + Arduino core sources, one LTO partition) | ~275,000 | yes |
| `libmbed.a`, prebuilt | ~180,000 | no |
| `libc.a` / `libm.a` / `libgcc.a` | ~63,000 | mostly no |
| `libstdc++.a` | ~56,000 | no |

Two facts govern any attempt to shrink the libc part.

**Almost every libc function is shared with Mbed.** `memcpy` comes in through
`rtx_kernel.o`, `memset` through `crt0.o`, `snprintf` through LWIP's DNS and MAC
formatting, and `strlen`, `strcmp`, `strncpy`, `memcmp`, `memmove`, `strchr`
each have live non-Botanik callers after `--gc-sections`. Removing every call
from our own code leaves the member linked and the byte count unchanged. Count
callers in the disassembled image before believing otherwise:

```bash
arm-none-eabi-objdump -d build-opta/botanik_opta.elf | grep -c 'bl.*<memcpy>$'
```

**`libstdc++` is one Mbed object's `std::string`.** `libbotanik` contains none.
`wiced_filesystem.o` pulls `string-inst.o` (57 KB), which cascades into
`stdexcept`, `cow-string-inst`, `random`, `system_error`, and from there libc's
`arc4random`, `signal` and `strerror`. `-fno-exceptions -fno-rtti` are already
set and cannot reach a prebuilt archive.

What was left, once those two are subtracted, was `strtod`, `strtoll` and
`strtoull` in `vars/scalar.cpp` and the double-precision libm in
`sun/sun_tracker.cpp`. All of it is ours now: `Utils::parse_u64` / `parse_i64` /
`parse_f64` and the single-precision solar trigonometry.

`strtod` was the expensive one, 6,488 bytes on full newlib and 11,204 under nano
(where `mprec` has no other caller once Mbed's `dtoa` is gone).

### Why parsing a real got cheap

`strtod` is correctly rounded **to double**, and that is its whole cost:
`mprec`'s arbitrary-precision `_Balloc` arithmetic exists for the last bit of a
53-bit mantissa. Nothing here wants a double. `parse_scalar()` narrows to
`float` on the next line.

An integer mantissa scaled once by a power of ten carries ~2e-16 relative error
in a double, against the 6e-8 a float needs: eight orders of headroom, so the
naive answer and the correctly rounded one agree on every float. The suite
asserts exactly that, differentially against the host's `strtod` over ~120,000
generated values, including full-precision float32 round-trips.

The one detail that is not free: scale in steps of at most 10^22, the largest
power of ten a double holds exactly. Clamping instead of stepping is a wrong
decade, not a rounding error.

## Optimization level

`BOTANIK_OPTA_OPT` sets it. `-Os` is the default and wins by a wide margin:

| | `.text` | vs `-Os` |
|---|---|---|
| `-Os` | 491,244 | |
| `-O2` | 580,332 | +18.1% |
| `-O3` | 661,316 | +34.6% |

The growth is entirely in our own code, which is one LTO partition: 275,088
bytes at `-Os` against 444,656 at `-O3`. `CMAKE_BUILD_TYPE` does not reach this
level, and never did: `target_compile_options` comes after the per-config flags
and GCC honors the last `-O`.

@note `-Ofast` is not on the table at any level: it implies `-ffast-math`, which
relaxes the FP semantics the metric filters rely on.

## newlib-nano

`BOTANIK_OPTA_NANO=ON` adds `--specs=nano.specs` beside the `nosys.specs` that
supplies the syscall stubs. It rewrites `-lc`, `-lstdc++` and `-lsupc++` to their
`_nano` variants.

| | `.text` | `.data` | flashable `.bin` |
|---|---|---|---|
| full newlib | 480,004 | 5,536 | 485,540 |
| newlib-nano | 444,288 | 3,848 | 448,136 |

It drops what is left of the float-conversion path (`dtoa` and the `mprec`
arbitrary-precision arithmetic under it), all three full `printf` engines
(`vfprintf`, `svfprintf`, `vfiprintf`), the `std::string` instantiations above,
and with them `strerror`, `locale` and the exception machinery.

Nothing in the image formats a float through `printf`, so losing nano's stubbed
`%f` costs nothing: every float we display goes through `Utils::format_float`,
which exists for that reason.

### The ABI question, and why the answer is yes

`libmbed.a` is prebuilt against full newlib, where `struct _reent` is 320 bytes;
nano's is 76. Code compiled against one layout that dereferences a field of the
other reads the wrong offset, silently.

It holds here because newlib pins the head of the struct. `sys/reent.h` puts
`_errno` first and `_stdin`/`_stdout`/`_stderr` next in **both** layouts, and
says so: "as an exception to the above put `_errno` first for binary
compatibility with non `_REENT_SMALL` targets". The only prebuilt Mbed code in
the image that dereferences `_reent` fields is `mbed_retarget.o`, in `_exit` and
`debug`, at +8 and +12 (`_stdout`, `_stderr`). Everything else loads the pointer
and hands it to `_malloc_r` / `_free_r`, which are nano's own.

Re-check that if the core is upgraded:

```bash
arm-none-eabi-objdump -d build-opta/botanik_opta.elf | less   # find &_impure_ptr
# then: any [rX, #n] with n > 12 in a non-libc function is a problem
```

@note The nano default is `OFF` until the image has run on hardware. Linking is
not running: nano's `malloc` is a different implementation, and no host test
covers the retarget path.
