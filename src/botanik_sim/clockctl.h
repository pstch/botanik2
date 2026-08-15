#ifndef BOTANIK_SIM_CLOCKCTL_H
#define BOTANIK_SIM_CLOCKCTL_H

/**
 * @file clockctl.h
 * @brief Simulator-only driver for the manual clock (BOTANIK_SIM_CLOCK=manual).
 *
 * With the manual clock the sim runs no faster than a harness tells it to: time
 * advances one beat per command, so a screen test asserts on a known number of
 * beats instead of waiting for the host to get round to it.
 *
 * Commands arrive on the fd named by BOTANIK_SIM_CONTROL_FD, one per line:
 *
 *     RUN <n>    run n beats, then acknowledge
 *     QUIT       stop the loop
 *
 * The ack is an OSC written to the CONSOLE, not to the control fd: two fds have
 * no ordering guarantee against each other, one stream does. Seeing the ack
 * therefore means every byte the beats produced is already in hand.
 *
 * It means "ready for a command", not "finished yours", and is sent once at
 * startup too. That is what makes a firmware reset survivable: the re-exec eats
 * whatever command was in flight, and the fresh process announcing itself is
 * what tells the harness to stop waiting for an ack that will never come.
 */

#ifdef TARGET_SIMULATOR

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "libbotanik/config/scheduler.h"
#include "libbotanik/hal/platform.h"

namespace Botanik { namespace Sim {

    /** @brief The ack the harness watches for. OSC 777, terminated by BEL. */
    inline const char* clockctl_ack_seq() { return "\033]777;botanik-ack\007"; }

    /** @brief Control fd, or -1 when the manual clock is not driven. */
    inline int& _ctl_fd() { static int fd = -2; return fd; }

    inline int ctl_fd() {
        if (_ctl_fd() == -2) {
            uint32_t fd = HAL::Env::u32("BOTANIK_SIM_CONTROL_FD", 0);
            _ctl_fd() = fd > 0 ? (int)fd : -1;    // 0 is stdin, i.e. unset
        }
        return _ctl_fd();
    }

    /** @brief Read one line from the control fd, blocking. False at EOF. */
    inline bool _ctl_read_line(char* out, size_t cap) {
        size_t n = 0;
        while (n + 1 < cap) {
            char c;
            ssize_t r = ::read(ctl_fd(), &c, 1);
            if (r <= 0) return false;          // EOF, or the harness went away
            if (c == '\n') break;
            out[n++] = c;
        }
        out[n] = '\0';
        return true;
    }

    /** @brief Block for the next command; returns the beats to run, 0 to stop. */
    inline int ctl_next_command() {
        char line[64];
        if (!_ctl_read_line(line, sizeof(line))) return 0;
        if (std::strncmp(line, "RUN ", 4) == 0) {
            int n = std::atoi(line + 4);
            return n > 0 ? n : 1;
        }
        return 0;                              // QUIT or anything unrecognised
    }

    /** @brief Advance the clock by exactly one beat period.
     *
     *  Exactly one, so the sim lands on the scheduler's own grid and the beat
     *  reads as on time: a bigger jump would present as an overrun and move the
     *  jitter and duty figures a screen test can see.
     */
    inline void ctl_advance_one_beat() {
        HAL::_virtual_ns() += (uint64_t)Config::TASK_PERIOD_MS * 1000000u;
    }

    /** @brief Tell the harness the requested beats are done and drained. */
    inline void ctl_ack() {
        const char* s = clockctl_ack_seq();
        ssize_t ignored = ::write(STDOUT_FILENO, s, std::strlen(s));
        (void)ignored;
    }

}}  // namespace Botanik::Sim

#endif  // TARGET_SIMULATOR
#endif  // BOTANIK_SIM_CLOCKCTL_H
