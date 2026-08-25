/** @file inputs.cpp
 *  @brief The local input terminals: levels, and the edge counters behind them.
 *
 *  Split from platform.cpp, which was over the file cap. Same
 *  `#ifdef TARGET_OPTA / #else` shape as its siblings; this pair needs neither
 *  termios nor mbed's C++ half, which is why it is the clean cut.
 */

#include "libbotanik/hal/platform.h"

#ifdef TARGET_OPTA

#include "Arduino.h"

namespace Botanik { namespace HAL {

    namespace DigitalIn {

        /** @brief The Arduino pin behind a local channel: I1..I8 are A0..A7.
         *  Kept per namespace rather than shared, so neither has to reach
         *  across the other's #if. */
        static int pin_of(uint8_t ch) { return A0 + (int)ch; }

        static bool g_attached[CHANNELS] = {};

        bool attach(uint8_t ch) {
            if (ch >= CHANNELS) return false;
            // INPUT, not INPUT_PULLUP: the OPTA terminals carry their own
            // conditioning, and a pull here fights it.
            pinMode(pin_of(ch), INPUT);
            g_attached[ch] = true;
            return true;
        }

        bool read(uint8_t ch) {
            if (ch >= CHANNELS || !g_attached[ch]) return false;
            return digitalRead(pin_of(ch)) == HIGH;
        }

        void inject(uint8_t, bool) {}   // the field drives the wire here

    }  // namespace DigitalIn

    namespace Pulse {

        /** @brief One counted channel. @c edges is written by an ISR and read
         *  by the loop, so it is volatile and read through a barrier - never a
         *  std::atomic, which would pull in a lock-free implementation the M7
         *  does not need for one aligned word. */
        struct Channel {
            volatile uint32_t edges    = 0;
            volatile uint32_t overruns = 0;
            volatile uint32_t last_us  = 0;   ///< for the resolution guard
            bool              attached = false;
        };

        static Channel g_ch[MAX_CHANNELS + 1];   // indexed by OPTA channel 0..7

        /** @brief Terminal I1 is A0 and I4 is A3; the two share an EXTI line on
         *  this part, so at most one of the pair may carry an interrupt. */
        static bool collides(uint8_t ch) {
            return (ch == 0 && g_ch[3].attached) || (ch == 3 && g_ch[0].attached);
        }

        /** @brief Shortest gap two edges may have and still both be counted.
         *  A contact-type meter bounces for a few hundred microseconds; a Hall
         *  one does not, but the guard costs nothing when it never fires. */
        static const uint32_t MIN_GAP_US = 250;

        template <uint8_t CH>
        static void isr() {
            Channel& c = g_ch[CH];
            const uint32_t now = (uint32_t)micros();
            if ((uint32_t)(now - c.last_us) < MIN_GAP_US) { c.overruns = c.overruns + 1; return; }
            c.last_us = now;
            c.edges   = c.edges + 1;
        }

        /** @brief The Arduino pin behind a local channel: I1..I8 are A0..A7. */
        static int pin_of(uint8_t ch) { return A0 + (int)ch; }

        /** @brief One ISR per channel, because attachInterrupt() takes a bare
         *  function pointer with no argument to carry the channel in. */
        static void (*const ISRS[8])() = {
            &isr<0>, &isr<1>, &isr<2>, &isr<3>, &isr<4>, &isr<5>, &isr<6>, &isr<7>
        };

        bool attach(uint8_t ch, Edge edge) {
            if (ch >= 8 || g_ch[ch].attached || collides(ch)) return false;
            const PinStatus mode = (edge == EDGE_FALLING) ? FALLING
                                 : (edge == EDGE_BOTH)    ? CHANGE : RISING;
            // attachInterrupt() sets the pull itself, per edge, and allocates an
            // mbed::InterruptIn. Both are why this belongs in setup().
            attachInterrupt(digitalPinToInterrupt(pin_of(ch)), ISRS[ch], mode);
            g_ch[ch].attached = true;
            return true;
        }

        Sample sample(uint8_t ch) {
            if (ch >= 8 || !g_ch[ch].attached) return Sample{ 0, Clock::uptime_ms() };
            // Re-read until stable: an edge landing between the count and the
            // clock would pair a new count with an old instant.
            for (;;) {
                const uint32_t a = g_ch[ch].edges;
                Memory::dmb();
                const uint64_t t = Clock::uptime_ms();
                Memory::dmb();
                if (g_ch[ch].edges == a) return Sample{ a, t };
            }
        }

        uint32_t overruns(uint8_t ch) {
            return (ch < 8) ? g_ch[ch].overruns : 0u;
        }

        void inject(uint8_t, uint32_t) {}   // the field pulses the wire here

    }  // namespace Pulse
}}

#else

namespace Botanik { namespace HAL {

    namespace DigitalIn {

        /** @brief No wire, so a terminal reads what Site::simulate() drives
         *  into it. Attaching still matters: an unattached channel reads false
         *  on both backends, which is what an unwired terminal does. */
        static bool g_attached[CHANNELS] = {};
        static bool g_level[CHANNELS]    = {};

        bool attach(uint8_t ch) {
            if (ch >= CHANNELS) return false;
            g_attached[ch] = true;
            return true;
        }

        bool read(uint8_t ch) {
            return (ch < CHANNELS && g_attached[ch]) ? g_level[ch] : false;
        }

        void inject(uint8_t ch, bool level) {
            if (ch < CHANNELS) g_level[ch] = level;
        }

    }  // namespace DigitalIn

    namespace Pulse {

        /** @brief The simulator has no wire, so a channel counts what
         *  Site::simulate() injects. Same shape as the OPTA's, so the code
         *  above it cannot tell which one it is talking to. */
        struct Channel {
            uint32_t edges    = 0;
            bool     attached = false;
            Edge     edge     = EDGE_RISING;
        };

        static Channel g_ch[8];

        static bool collides(uint8_t ch) {
            return (ch == 0 && g_ch[3].attached) || (ch == 3 && g_ch[0].attached);
        }

        bool attach(uint8_t ch, Edge edge) {
            // The I1/I4 rule is enforced HERE too, though nothing on a host
            // shares a line: a site that only ever runs in the simulator would
            // otherwise discover it on the bench.
            if (ch >= 8 || g_ch[ch].attached || collides(ch)) return false;
            g_ch[ch].attached = true;
            g_ch[ch].edge     = edge;
            return true;
        }

        Sample sample(uint8_t ch) {
            const uint64_t t = Clock::uptime_ms();
            return Sample{ (ch < 8 && g_ch[ch].attached) ? g_ch[ch].edges : 0u, t };
        }

        uint32_t overruns(uint8_t) { return 0; }

        void inject(uint8_t ch, uint32_t edges) {
            if (ch < 8 && g_ch[ch].attached) g_ch[ch].edges += edges;
        }

    }  // namespace Pulse
}}

#endif // TARGET_OPTA
