#ifndef TESTS_SCENARIO_TRACE_H
#define TESTS_SCENARIO_TRACE_H

/**
 * @file scenario_trace.h
 * @brief What the scenario cases hand the report generator.
 *
 * Off unless BOTANIK_SCENARIO_TRACE names a file, so a normal `make check` pays
 * nothing and the cases stay tests rather than a reporting tool. What is written
 * is what the ASSERTIONS were made against: the report cannot say the plant did
 * something the suite did not just watch it do.
 *
 * One tagged record per line, in the order they happen:
 *   S|<scenario>|<what it is about>
 *   W|<label>|<value>            the weather the case is holding
 *   M|<moment>                   a point the case stopped to look
 *   V|<row>|<value>|<quality>    one row, as the console would show it
 *   N|<note>                     what the case asserted there, in words
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "libbotanik.h"

namespace ScenarioTrace {

inline std::FILE* out() {
    static std::FILE* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char* path = std::getenv("BOTANIK_SCENARIO_TRACE");
        if (path != nullptr && path[0] != '\0') f = std::fopen(path, "a");
    }
    return f;
}

inline bool on() { return out() != nullptr; }

inline void scenario(const char* name, const char* about) {
    if (on()) std::fprintf(out(), "S|%s|%s\n", name, about);
}
inline void weather(const char* label, const char* value) {
    if (on()) std::fprintf(out(), "W|%s|%s\n", label, value);
}
inline void moment(const char* what) {
    if (on()) std::fprintf(out(), "M|%s\n", what);
}
inline void note(const char* what) {
    if (on()) std::fprintf(out(), "N|%s\n", what);
}

/** @brief One row, rendered the way the console renders it: a word for an enum,
 *  yes/no for a coil, the value and its unit otherwise - and what the quality
 *  says when it is not simply good. */
inline void row(Botanik::Vars::Bank& io, const char* name) {
    if (!on()) return;
    const Botanik::Vars::Var h = io.var(io.find(name));
    if (h.null()) return;

    char val[64];
    const Botanik::Vars::Value v = h.value();
    if (h.meta().labels != nullptr) {
        std::snprintf(val, sizeof val, "%s", v.label());
    } else if (h.meta().type == Botanik::Vars::T_BOOL) {
        std::snprintf(val, sizeof val, "%s", h.as_bool() ? "yes" : "no");
    } else {
        const char* unit = h.unit();
        std::snprintf(val, sizeof val, "%.2f%s%s", (double)h.as_f32(),
                      (unit != nullptr && unit[0] != '\0') ? " " : "",
                      (unit != nullptr) ? unit : "");
    }
    const char* q = h.valid() ? ""
                             : Botanik::Vars::quality_label((uint64_t)v.q);
    std::fprintf(out(), "V|%s|%s|%s\n", name, val, q);
}

}  // namespace ScenarioTrace

#endif // TESTS_SCENARIO_TRACE_H
