// REVIEW_35: DONE
#ifndef LOGS_FORMAT_H
#define LOGS_FORMAT_H

/**
 * @file format.h
 * @brief Log-entry formatting: assemble the `TIMESTAMP LEVEL TOPIC MESSAGE`
 *        line.
 */

#include <algorithm>                // std::min
#include <cstdint>                  // uint64_t
#include <cstdio>                   // snprintf

#include "libbotanik/logs/entry.h"  // Logs::LogEntry
#include "libbotanik/utils/time.h"  // Utils::format_ms_timestamp

namespace Botanik { namespace Logs {

    /** @brief Format a log level into a short prefix.  */
    inline const char* format_log_level(Logs::LogLevel level) { switch (level) {
        case Logs::LOG_DEBUG:      return "[~]";
        case Logs::LOG_INFO:       return "[-]";
        case Logs::LOG_NOTICE:     return "[+]";
        case Logs::LOG_WARNING:    return "[!]";
        case Logs::LOG_ERROR:      return "[*]";
        case Logs::LOG_CRITICAL:   return "[@]";
        default:                    return "[?]";
    } }

    /** @brief Per-topic strings, indexed by LogTopic (dense 0..STORAGE):
     *  { display label, lowercase MQTT segment }. One table, so the two can no
     *  longer drift apart. */
    static const char* const TOPIC_NAMES[Logs::LOG_TOPIC_COUNT][2] = {
        { "",            "none"        },
        { "MODBUS",      "modbus"      },
        { "BOOT",        "boot"        },
        { "NETWORK",     "network"     },
        { "TIME",        "time"        },
        { "STATUS",      "status"      },
        { "ASSERT",      "assert"      },
        { "ALARM_BEG",   "alarm_beg"   },
        { "ALARM_END",   "alarm_end"   },
        { "ALARM_FLAP",  "alarm_flap"  },
        { "HEALTH",      "health"      },
        { "STORAGE",     "storage"     },
    };

    /** @brief Format a log topic into its actual name. */
    inline const char* format_log_topic(Logs::LogTopic topic) {
        return topic < Logs::LOG_TOPIC_COUNT ? TOPIC_NAMES[topic][0] : "";
    }

    /** @brief Lowercase level word, for MQTT topic segments. */
    inline const char* format_log_level_name(Logs::LogLevel level) { switch (level) {
        case Logs::LOG_DEBUG:      return "debug";
        case Logs::LOG_INFO:       return "info";
        case Logs::LOG_NOTICE:     return "notice";
        case Logs::LOG_WARNING:    return "warning";
        case Logs::LOG_ERROR:      return "error";
        case Logs::LOG_CRITICAL:   return "critical";
        default:                    return "unknown";
    } }

    /** @brief Lowercase topic word, for MQTT topic segments (ALARM_BEG -> "alarm_beg",
     *  matching the ALARM_BEG display label). */
    inline const char* format_log_topic_name(Logs::LogTopic topic) {
        return topic < Logs::LOG_TOPIC_COUNT ? TOPIC_NAMES[topic][1] : "unknown";
    }

    /** @name Wrapped-row continuation prefixes and the width budget
     *
     *  What a page puts at the start of a wrapped row. It lives HERE, beside
     *  the format string that sets the width, because sized anywhere else it
     *  goes quietly out of alignment the next time the prefix moves.
     *
     *  ## What the prefix costs
     *
     *  format_log_entry() below emits `"%s %s %s%s %s"`, so the message starts
     *  at column `28 + strlen(topic)`, or 27 with no topic (the ":" is skipped).
     *  At CONSOLE_COLS = 80 that leaves the FIRST line:
     *
     *      no topic ............................ 53
     *      BOOT, TIME .......................... 48
     *      MODBUS, STATUS, ASSERT, HEALTH ...... 46
     *      NETWORK, STORAGE .................... 45
     *      ALARM_BEG, ALARM_END ................ 43
     *      ALARM_FLAP .......................... 42
     *
     *  A continuation row gets `80 - strlen(LOG_CONT_PREFIX)` = 54, which is
     *  WIDER than any first line. That is what makes lead-plus-detail work.
     *
     *  ## The rule for writing a message
     *
     *  Keep the first line at or under **40 characters** - below every topic's
     *  real budget, so a line survives a topic being renamed - and put the rest
     *  after an explicit '\n'. The lead is what an operator scans for; the
     *  number an engineer goes looking for belongs on line 2, whole, where it
     *  cannot be split across a fold. A variable-width "%s" (a device label, an
     *  IP, a variable name) does not go on line 1 unless the fixed text around
     *  it leaves room for its worst case. Reword before breaking when the
     *  rewording is the better sentence anyway.
     *
     *  There is no constant for the 40: CONSOLE_COLS is site-overridable and
     *  the console is resizable at runtime, so a hard number would be a lie -
     *  and nothing can consult a constant from inside a string literal.
     *
     *  ## Why LOG_CONT_PREFIX is 26 and not 34
     *
     *  It does NOT line up under the start of the message: at 26 it sits under
     *  the TOPIC, short by 6 to 12 for every non-empty
     *  topic. Deliberately left there. store_wrapped() takes one prefix per
     *  entry while the message column is topic-dependent, so aligning it means
     *  threading the topic through LineBuffer and its three call sites; and
     *  widening it to 34 would cut the continuation budget from 54 to 46, which
     *  is the headroom every two-line message above is written against. A fixed
     *  left edge marked "> " also scans better than a per-topic ragged one.
     *  @{ */
    static constexpr const char* CONT_PREFIX     = "> ";
    static constexpr const char* LOG_CONT_PREFIX = "                        > ";
    /** @} */

    /** @brief Rewrite every '\n' in `[buf, buf+n)` as `" | "`, in place.
     *
     *  A '\n' in a message is a DISPLAY directive, for the console renderer to
     *  fold into a LOG_CONT_PREFIX row. A wire sink has no such row: the break
     *  would put half the entry on a second physical line carrying no
     *  timestamp, level or topic - unparseable to a collector and invisible to
     *  a grep on the topic. Folding keeps one entry to one record.
     *
     *  @param cap Bytes available in @p buf. Each break grows the text by 2, so
     *             the tail is truncated rather than overrun; LOG_FORMATTED_LEN
     *             is only exactly LOG_MESSAGE_LEN + 64, which is not headroom
     *             to assume. @return the new length, never counting a NUL.
     */
    inline size_t fold_newlines(char* buf, size_t n, size_t cap) {
        if (buf == nullptr || cap == 0) return 0;
        if (n > cap) n = cap;
        // Right to left, so a byte still to be read is never one just shifted.
        // Count first to know where the text ends; a position past cap is
        // simply not written, which truncates the TAIL and keeps the head.
        size_t breaks = 0;
        for (size_t i = 0; i < n; ++i) if (buf[i] == '\n') breaks++;
        if (breaks == 0) return n;

        static const char SEP[3] = { ' ', '|', ' ' };
        const size_t len = n + 2 * breaks;   // where the text WOULD end
        size_t out = len;
        size_t src = n;                      // one past the byte to read next
        while (src > 0) {
            const char c = buf[--src];
            if (c != '\n') {
                if (--out < cap) buf[out] = c;
            } else {
                for (int k = 2; k >= 0; --k)
                    if (--out < cap) buf[out] = SEP[k];
            }
        }
        return len < cap ? len : cap;
    }

    /** @brief Format a complete log entry as `TIMESTAMP LEVEL TOPIC MESSAGE`.
     *  @return Bytes of TEXT written, never counting the NUL; 0 on encoding
     *          error. Clamped to `len - 1` on truncation, so a caller that
     *          writes the result to a sink cannot push the terminator onto the
     *          wire. */
    inline size_t format_log_entry(const Logs::LogEntry* entry, char* buf, size_t len) {
        using namespace Botanik::Utils;
        // format the log level and log topic of the log entry
        const char* level = format_log_level(entry->level);
        const char* topic = format_log_topic(entry->topic);
        // skip the separator if the topic is empty
        const char* topic_sep = (topic[0] != '\0') ? ":" : "";
        // format the timestamp
        uint64_t timestamp = entry->timestamp;
        char time_buf[TIMESTAMP_LEN];
        format_ms_timestamp(timestamp, time_buf, sizeof(time_buf));
        // build the formatted log entry
        int written = snprintf(buf, len, "%s %s %s%s %s",
                               time_buf, level, topic, topic_sep, entry->message);
        // Return the written bytes, or 0 on encoding error. snprintf answers
        // what it WOULD have written, so on truncation clamp to the text that
        // actually landed - len-1, the NUL is not ours to emit.
        if (written < 0 || len == 0) return 0;
        return std::min((size_t)written, len - 1);
    }

}}

#endif // LOGS_FORMAT_H