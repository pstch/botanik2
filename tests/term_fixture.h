#ifndef BOTANIK_TEST_TERM_FIXTURE_H
#define BOTANIK_TEST_TERM_FIXTURE_H

/**
 * @file term_fixture.h
 * @brief PipeSerial - a BufferedSerial whose wire is a pipe the test can read.
 *
 * Every screen-level fixture needs the same plumbing: a HAL::Serial::Raw bound
 * to a non-blocking pipe, a BufferedSerial over it, and a way to drain the TX
 * ring into a string. This is that plumbing only; a fixture holds one and adds
 * whatever it builds on top.
 */

#include <fcntl.h>                          // fcntl / O_NONBLOCK
#include <unistd.h>                         // pipe / read / close

#include <string>                           // std::string

#include "doctest.h"                        // REQUIRE
#include "libbotanik/hal/platform.h"        // HAL::Serial::Raw
#include "libbotanik/utils/serial.h"        // Utils::BufferedSerial

namespace botanik_test {

    /** @brief A BufferedSerial whose far end is a pipe this object owns.
     *
     *  The read end is non-blocking, so capture() never hangs on a fixture that
     *  emitted nothing - which is what half the byte-budget tests assert. */
    struct PipeSerial {
        int                           fds[2];
        Botanik::HAL::Serial::Raw     raw;
        Botanik::Utils::BufferedSerial serial;

        PipeSerial() : serial(&raw) {
            REQUIRE(::pipe(fds) == 0);
            ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
            raw.set_fds(fds[0], fds[1]);
        }
        ~PipeSerial() { ::close(fds[0]); ::close(fds[1]); }

        PipeSerial(const PipeSerial&) = delete;
        PipeSerial& operator=(const PipeSerial&) = delete;

        /** @brief Push the TX ring to the pipe and return what accumulated.
         *
         *  BufferedSerial hands the driver a bounded slice per call, so the 64
         *  calls pay off the per-activation cap: one call would leave a long
         *  frame half-written and the test would assert on a fragment. */
        std::string capture() {
            std::string out;
            char buf[8192];
            for (int i = 0; i < 64; i++) serial.transmit();
            ssize_t n;
            while ((n = ::read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
            return out;
        }

        /** @brief Feed @p s to the RX side, as if it had arrived on the wire. */
        void feed(const char* s, size_t n) {
            ssize_t w = ::write(fds[1], s, n);
            (void)w;
            serial.receive();
        }
    };

}  // namespace botanik_test

#endif // BOTANIK_TEST_TERM_FIXTURE_H
