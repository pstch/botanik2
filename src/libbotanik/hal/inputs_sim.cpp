/** @file inputs_sim.cpp
 *  @brief The local input terminals: levels, and the edge counters behind them.
 *
 *  Split from the platform backends, which were over the file cap. Same
 *  `#ifdef TARGET_OPTA / #else` shape as its siblings; this pair needs neither
 *  termios nor mbed's C++ half, which is why it is the clean cut.
 */

#include "libbotanik/hal/platform.h"

#include <cstdio>   // the simulator traces an output change to stderr

#ifdef TARGET_SIMULATOR

namespace Botanik { namespace HAL {

    namespace DigitalIn {

        /** @brief No wire, so a terminal reads what Site::simulate() drives
         *  into it. Attaching still matters: an unattached pin reads false on
         *  both backends, which is what an unwired terminal does. */
        static bool g_attached[Pin::COUNT] = {};
        static bool g_level[Pin::COUNT]    = {};

        bool attach(uint16_t pin) {
            if (!owns(pin)) return false;
            g_attached[pin] = true;
            return true;
        }

        bool read(uint16_t pin) {
            return (owns(pin) && g_attached[pin]) ? g_level[pin] : false;
        }

        void inject(uint16_t pin, bool level) {
            if (owns(pin)) g_level[pin] = level;
        }

    }  // namespace DigitalIn

    namespace DigitalOut {

        /** @brief Nothing to energise, so the level is the whole of it - and
         *  reading it back is how a screen test asserts that a law drove a
         *  relay. Same shape as the OPTA's, which keeps its own shadow for the
         *  same reason: a coil is not readable. */
        static bool g_attached[Pin::COUNT] = {};
        static bool g_level[Pin::COUNT]    = {};

        bool attach(uint16_t pin) {
            if (!owns(pin)) return false;
            g_attached[pin] = true;
            g_level[pin]    = false;
            return true;
        }

        /** @brief The label the trace names a pin by, so a line reads as the
         *  site's table wrote it rather than as an index. */
        static const char* label(uint16_t pin) {
            switch (pin) {
                case Pin::RELAY_1: return "RELAY_1";
                case Pin::RELAY_2: return "RELAY_2";
                case Pin::RELAY_3: return "RELAY_3";
                case Pin::RELAY_4: return "RELAY_4";
                case Pin::LED_1:   return "LED_1";
                case Pin::LED_2:   return "LED_2";
                case Pin::LED_3:   return "LED_3";
                case Pin::LED_4:   return "LED_4";
                case Pin::LED_BLUE:return "LED_BLUE";
                default:           return "?";
            }
        }

        void write(uint16_t pin, bool on) {
            if (!owns(pin) || !g_attached[pin]) return;
            // ON CHANGE, to stderr, the way the mock network and the field-bus
            // self-check report: there is no lamp to look at and no coil to
            // hear, so a local output is otherwise invisible in a sim run. The
            // console is on stdout, so this never lands in a screen capture.
            if (g_level[pin] != on)
                fprintf(stderr, "[local-out] %s %s\n", label(pin), on ? "ON" : "off");
            g_level[pin] = on;
        }

        bool read(uint16_t pin) {
            return owns(pin) && g_attached[pin] && g_level[pin];
        }

    }  // namespace DigitalOut

    namespace AnalogIn {

        /** @brief No wire, so a terminal reads what Site::simulate() drives in.
         *  Counts, not volts: the calibration is the ROW's, and a site that
         *  animates its own field has to go through the same scaling its
         *  hardware will. */
        static bool     g_attached[DigitalIn::CHANNELS] = {};
        static uint16_t g_count[DigitalIn::CHANNELS]    = {};

        bool attach(uint16_t pin) {
            if (!owns(pin)) return false;
            g_attached[pin] = true;
            return true;
        }

        uint16_t read(uint16_t pin) {
            return (owns(pin) && g_attached[pin]) ? g_count[pin] : 0;
        }

        void inject(uint16_t pin, uint16_t count) {
            if (owns(pin)) g_count[pin] = (count > FULL_SCALE) ? FULL_SCALE : count;
        }

    }  // namespace AnalogIn

    namespace Pulse {

        /** @brief The simulator has no wire, so a channel counts what
         *  Site::simulate() injects. Same shape as the OPTA's, so the code
         *  above it cannot tell which one it is talking to. */
        struct Channel {
            uint32_t edges    = 0;
            bool     attached = false;
            Edge     edge     = EDGE_RISING;
        };

        static Channel g_ch[CHANNELS];

        static bool collides(uint8_t ch) {
            return (ch == 0 && g_ch[3].attached) || (ch == 3 && g_ch[0].attached);
        }

        bool attach(uint8_t ch, Edge edge) {
            // The I1/I4 rule is enforced HERE too, though nothing on a host
            // shares a line: a site that only ever runs in the simulator would
            // otherwise discover it on the bench.
            // IDEMPOTENT: a channel already counting the SAME edge is already
            // in the state asked for, so this SUCCEEDED. Bring-up runs twice in
            // one process every time a test builds a second firmware, and a
            // caller cannot tell "already yours" from "out of range" through a
            // bool. Asking for a DIFFERENT edge on a live channel is a real
            // conflict and still fails.
            if (ch < CHANNELS && g_ch[ch].attached) return g_ch[ch].edge == edge;
            if (ch >= CHANNELS || collides(ch)) return false;
            g_ch[ch].attached = true;
            g_ch[ch].edge     = edge;
            return true;
        }

        Sample sample(uint8_t ch) {
            const uint64_t t = Clock::uptime_ms();
            return Sample{ (ch < CHANNELS && g_ch[ch].attached) ? g_ch[ch].edges : 0u, t };
        }

        uint32_t overruns(uint8_t) { return 0; }

        void inject(uint8_t ch, uint32_t edges) {
            if (ch < CHANNELS && g_ch[ch].attached) g_ch[ch].edges += edges;
        }

    }  // namespace Pulse
}}

#endif // TARGET_SIMULATOR
