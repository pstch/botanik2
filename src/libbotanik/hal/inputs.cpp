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

    /** @brief HAL::Pin -> the board's own number. The ONE place the two
     *  vocabularies meet, and the reason a site's tables can name a terminal
     *  without Arduino.h reaching them. -1 for a label this board does not
     *  have, which every caller treats as "not mine". */
    static int board_pin(uint16_t pin) {
        switch (pin) {
            case Pin::IN1: case Pin::IN2: case Pin::IN3: case Pin::IN4:
            case Pin::IN5: case Pin::IN6: case Pin::IN7: case Pin::IN8:
                return A0 + (int)pin;
            case Pin::BUTTON:   return BTN_USER;
            case Pin::RELAY_1: return RELAY1;
            case Pin::RELAY_2: return RELAY2;
            case Pin::RELAY_3: return RELAY3;
            case Pin::RELAY_4: return RELAY4;
            case Pin::LED_1:   return LED_D0;
            case Pin::LED_2:   return LED_D1;
            case Pin::LED_3:   return LED_D2;
            case Pin::LED_4:   return LED_D3;
            case Pin::LED_BLUE: return LED_USER;
            default:            return -1;
        }
    }

    namespace DigitalIn {

        static bool g_attached[Pin::COUNT] = {};

        bool attach(uint16_t pin) {
            const int p = owns(pin) ? board_pin(pin) : -1;
            if (p < 0) return false;
            // INPUT, not INPUT_PULLUP: the OPTA terminals carry their own
            // conditioning, and a pull here fights it. The BUTTON is the
            // exception - it is a bare contact to ground, so it needs the pull
            // the terminals must not have.
            pinMode(p, pin == Pin::BUTTON ? INPUT_PULLUP : INPUT);
            g_attached[pin] = true;
            return true;
        }

        bool read(uint16_t pin) {
            if (!owns(pin) || !g_attached[pin]) return false;
            return digitalRead(board_pin(pin)) == HIGH;
        }

        void inject(uint16_t, bool) {}   // the field drives the wire here

    }  // namespace DigitalIn

    namespace DigitalOut {

        static bool g_attached[Pin::COUNT] = {};
        static bool g_level[Pin::COUNT]    = {};

        bool attach(uint16_t pin) {
            const int p = owns(pin) ? board_pin(pin) : -1;
            if (p < 0) return false;
            pinMode(p, OUTPUT);
            digitalWrite(p, LOW);          // a coil comes up de-energised
            g_attached[pin] = true;
            g_level[pin]    = false;
            return true;
        }

        void write(uint16_t pin, bool on) {
            if (!owns(pin) || !g_attached[pin]) return;
            digitalWrite(board_pin(pin), on ? HIGH : LOW);
            g_level[pin] = on;
        }

        bool read(uint16_t pin) {
            return owns(pin) && g_attached[pin] && g_level[pin];
        }

    }  // namespace DigitalOut

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
            Edge              edge     = EDGE_RISING;
        };

        static Channel g_ch[CHANNELS];

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
        static void (*const ISRS[CHANNELS])() = {
            &isr<0>, &isr<1>, &isr<2>, &isr<3>, &isr<4>, &isr<5>, &isr<6>, &isr<7>
        };

        bool attach(uint8_t ch, Edge edge) {
            // IDEMPOTENT: a channel already counting the SAME edge is already
            // in the state asked for, so this SUCCEEDED. Bring-up runs twice in
            // one process every time a test builds a second firmware, and a
            // caller cannot tell "already yours" from "out of range" through a
            // bool. Asking for a DIFFERENT edge on a live channel is a real
            // conflict and still fails.
            if (ch < CHANNELS && g_ch[ch].attached) return g_ch[ch].edge == edge;
            if (ch >= CHANNELS || collides(ch)) return false;
            const PinStatus mode = (edge == EDGE_FALLING) ? FALLING
                                 : (edge == EDGE_BOTH)    ? CHANGE : RISING;
            // attachInterrupt() sets the pull itself, per edge, and allocates an
            // mbed::InterruptIn. Both are why this belongs in setup().
            attachInterrupt(digitalPinToInterrupt(pin_of(ch)), ISRS[ch], mode);
            g_ch[ch].attached = true;
            g_ch[ch].edge     = edge;
            return true;
        }

        Sample sample(uint8_t ch) {
            if (ch >= CHANNELS || !g_ch[ch].attached) return Sample{ 0, Clock::uptime_ms() };
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
            return (ch < CHANNELS) ? g_ch[ch].overruns : 0u;
        }

        void inject(uint8_t, uint32_t) {}   // the field pulses the wire here

    }  // namespace Pulse
}}

#else

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

        void write(uint16_t pin, bool on) {
            if (owns(pin) && g_attached[pin]) g_level[pin] = on;
        }

        bool read(uint16_t pin) {
            return owns(pin) && g_attached[pin] && g_level[pin];
        }

    }  // namespace DigitalOut

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

#endif // TARGET_OPTA
