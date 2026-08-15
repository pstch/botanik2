// REVIEW_35: TODO
#ifndef HAL_OPTA_H
#ifdef TARGET_OPTA
#define HAL_OPTA_H

/**
 * @file opta.h
 * @brief HAL backend for the Arduino OPTA (STM32H747 dual-core ARM).
 *
 * Implements the platform.h interface on top of Mbed OS and the Arduino core.
 * Selected automatically when TARGET_OPTA is defined; see platform.h.
 */

// assert() - HAL sits below the logger, so it can't use BOTANIK_ASSERT
#include <cassert>
#include <chrono>
#include <cstdint>
#include <type_traits>

// CMSIS only - resolved from the installed mbed_opta core, not this tree. NOT
// mbed.h: what must be inline here (__DSB, __DMB, ARM_MPU_OrderedMemcpy,
// SCB_*DCache_by_Addr, __BKPT, CoreDebug) is all CMSIS, and the two pieces of
// Mbed C++ the HAL needs live in platform.cpp - see its file header.
#include "stm32h7xx.h"


namespace Botanik { namespace HAL {

    /** @name Hardware identity
     *  @{ */

    const char* hardware_description();

    /** @} */

    /** @brief Stack region introspection. */
    namespace Stack {

    uintptr_t base();

    uint32_t depth();

    /** @brief High-water stack usage (bytes) of the current RTOS thread, from
     *  RTX's own watermark (RTX paints each thread's stack at creation, so no
     *  manual painting on hardware). Returns 0 if the watermark is unavailable
     *  (RTX tracking disabled). */
    uint32_t high();

    }  // namespace Stack

    /** @brief Timing and clocks. */
    namespace Clock {

    /*  These read mbed::LowPowerClock, the only reason this header would need
     *  mbed.h, so they are defined in platform.cpp instead. The extra call is
     *  noise: uptime_ms() is already three out-of-line calls deep reading a
     *  32.768 kHz peripheral. */

    uint32_t current_ms();

    uint64_t uptime_ms();

    uint64_t uptime_us();

    uint64_t wall_ms();

    void set_wall_ms(uint64_t unix_ms);

    void sleep_ms(uint32_t ms);

    }  // namespace Clock

    /** @brief Memory-ordering primitives and D-cache maintenance. */
    namespace Memory {

    inline void dsb() {
        __DSB();
    }

    inline void dmb() {
        __DMB();
    }

    // @warning dst and src must not alias: ARM_MPU_OrderedMemcpy declares src
    // __RESTRICT, so an in-place/overlapping copy is UB on the optimized path.
    template <typename T>
    inline void ordered_copy(T* dst, const T* src) {
        // ARM_MPU_OrderedMemcpy counts 32-bit words, not bytes.
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_copy needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
#if defined(DEBUG)
        assert((const void*)dst != (const void*)src);   // no aliasing (see @warning)
#endif
        ARM_MPU_OrderedMemcpy((volatile uint32_t*)dst, (const uint32_t*)src,
                              sizeof(T) / sizeof(uint32_t));
        __DSB();
    }

    template <typename T>
    inline void ordered_move(T* dst, T src) {
        static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t) == 0,
                      "ordered_move needs a word-multiple T");
        if (std::is_empty<T>::value) return;   // empty payload carries no data
        *dst = src;
        __DSB();
    }

    template <typename T>
    inline T ordered_read(const volatile T* src) {
        T value = *src;   // volatile load: the compiler must re-read it
        __DMB();          // acquire: order later accesses after this read
        return value;
    }

    // Cache discipline for a cross-core shared region. clean (write-back) is
    // always safe: it rounds to whole lines and only ever writes back.
    // invalidate DISCARDS whole lines, so it is safe ONLY on a region we treat
    // as read-only: invalidate what the other core produces, clean what we
    // produce, and never let the two regions share a line (align each to 32).
    // @warning never invalidate a line holding data we wrote; that write is
    // silently dropped.
    inline void cache_clean(const void* addr, size_t len) {
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        // CMSIS rounds to lines
        SCB_CleanDCache_by_Addr((void*)addr, (int32_t)len);
    #else
        (void)addr; (void)len;
    #endif
    }

    inline void cache_invalidate(const void* addr, size_t len) {
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_InvalidateDCache_by_Addr((void*)addr, (int32_t)len);
    #else
        (void)addr; (void)len;
    #endif
    }

    }  // namespace Memory

    /** @brief Debugger interaction. */
    namespace Debug {

    void breakpoint();

    bool under_debugger();

    }  // namespace Debug

    /** @brief System lifecycle (reset / shutdown). */
    namespace System {

    void reset();

    void shutdown(int code);

    }  // namespace System

    /** @brief Hardware watchdog. */
    namespace Watchdog {

    // Wrap the Mbed OS Watchdog singleton (the STM32H7 IWDG). Compile-reasoned
    // only - confirm the API and timeout ceiling on a real arm-none-eabi build.

    void start(uint32_t timeout_ms);

    /** @brief Reset the countdown; called once per beat. Out of line for the
     *  same reason as the clocks: it is the Mbed Watchdog singleton. */
    void kick();

    void stop();

    }  // namespace Watchdog

    /**
     * @brief Serial access.
     *
     * CONSOLE and MONITOR are *logical* endpoints, each backed by a concrete
     * transport behind one small interface, so which physical port serves which
     * role is a one-line change in get(). Today: CONSOLE on the hardware UART
     * (Mbed's own CONSOLE_TX/RX for this target, PG_14 / PG_9), MONITOR on USB
     * CDC.
     *
     * Pure Mbed - no Arduino core. Virtual dispatch is cheap here: two
     * long-lived RAM objects, not a flash-resident descriptor table where a vptr
     * would force it out of .rodata.
     */
    namespace Serial {

    /** @brief The transport-independent surface Utils::BufferedSerial drives. */
    class Raw {
    public:
        virtual ~Raw() {}
        /** @brief Bring the port up at @p baud. Transports with no line rate
         *  (USB CDC) ignore it. */
        virtual void     begin(uint32_t baud) = 0;
        /** @brief Next buffered byte, or -1 when there is none. */
        virtual int      read() = 0;
        /** @brief Queue one byte; returns the number accepted (0 or 1). */
        virtual size_t   write(uint8_t c) = 0;
        /** @brief Non-zero while the transport can take more output. Gates the
         *  TX drain loop, which writes a byte at a time and re-checks. */
        virtual uint16_t availableForWrite() = 0;
    };

    /** @brief Line format parity. The field bus needs it (Modbus RTU's default
     *  is 8E1); CONSOLE and MONITOR are 8N1 and never ask. */
    enum Parity : uint8_t { PARITY_NONE = 0, PARITY_EVEN, PARITY_ODD };

    /** @brief Logical serial endpoints: CONSOLE drives the interactive
     *  terminal, MONITOR the secondary diagnostic/log output, FIELDBUS the
     *  RS-485 bus the Modbus RTU client talks on. */
    enum Port { CONSOLE, MONITOR, FIELDBUS };

    /** @brief Resolve a logical Port to its transport. The concrete UART / USB
     *  CDC transports live in platform.cpp: they hold mbed::BufferedSerial and
     *  USBSerial members, and nothing outside get() names them. */
    Raw* get(Port port);

    /** @brief Neither transport is an interactive terminal, so diagnostic
     *  output is never suppressed on OPTA. Mirrors the simulator query so sinks
     *  (e.g. the MONITOR SerialLogWriter) stay platform-independent. */
    bool is_output_tty(const Raw*);

    /** @name Field bus control
     *
     *  Free functions rather than virtuals on Raw: Raw is the surface
     *  BufferedSerial drives for EVERY port, and widening it would make CONSOLE
     *  and MONITOR implement a line format and a turnaround they do not have.
     *  The simulator backend declares the same three, so a caller never branches
     *  on the target. Defined in platform.cpp, where the concrete Rs485Raw is.
     *  @{ */

    /** @brief Bring the bus up at @p baud with the given frame format. */
    void fieldbus_begin(uint32_t baud, uint8_t data_bits, Parity parity,
                        uint8_t stop_bits);

    /** @brief Whether everything written has finished leaving the wire. */
    bool fieldbus_tx_idle();

    /** @brief Whether the receiver hears our own transmission on this port.
     *
     *  TRUE here: RE (PB_13) is held asserted for the life of the port, so our
     *  own frame arrives back at the UART ahead of any answer, exactly as long
     *  as what was written - which lets the master count it off rather than try
     *  to recognise it. Deassert RE around transmission instead and this becomes
     *  false. See the RS-485 notes in platform.cpp. */
    bool fieldbus_echoes();

    /** @brief Discard whatever is sitting in the receive path.
     *
     *  Called once a request has drained and before its answer is expected: on a
     *  bus whose receiver never stops listening, that is when our own echo is
     *  sitting there. Keeps a write echo from being taken for the response to
     *  it, which for FC05 and FC06 would be byte-identical and silently wrong. */
    uint16_t fieldbus_flush_input();

    /** @} */

    }  // namespace Serial


}}

#endif // TARGET_OPTA
#endif // HAL_OPTA_H
