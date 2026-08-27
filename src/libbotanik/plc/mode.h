#ifndef PLC_MODE_H
#define PLC_MODE_H

/**
 * @file mode.h
 * @brief Plc::Mode - what a control program has been asked to still be doing.
 * @note A leaf, so tasks/ and shell/ can name the type without a cycle back
 *       through program.h. Never forward-declare the enum in those headers: an
 *       opaque re-declaration in N places is a silent ODR break the day the
 *       underlying type changes.
 */

#include <cstdint>

namespace Botanik { namespace Plc {

    /** @name The mode ladder
     *  @{ */

    /**
     * @brief Four rungs, each taking away one more thing.
     *
     * | rung | run() | observe() | outputs | overrides |
     * |---|---|---|---|---|
     * | NORMAL | yes | yes | driven by the law | drive |
     * | INHIBITED | no | yes | held where the law left them | drive |
     * | SAFE | no | yes | dropped to each row's declared state | muted |
     * | DISABLED | no | no | the same, and its other rows held too | muted |
     *
     * INHIBITED is how a loop is taken BY HAND: the law stops and the outputs
     * freeze, so an override is the only thing left that can move them - which
     * is why it is the one withheld rung that does not mute them.
     *
     * @note Ordered STRICTEST FIRST, so weighing several asks is a minimum.
     */
    enum class Mode : uint8_t {
        DISABLED = 0,
        SAFE     = 1,
        INHIBITED = 2,
        NORMAL   = 3
    };

    /** @brief The stricter of two asks, which is the lower rung. */
    inline Mode stricter(Mode a, Mode b) { return (a < b) ? a : b; }

    /** @brief @p m as a word, for the shell and the logs. */
    inline const char* mode_name(Mode m) {
        switch (m) {
            case Mode::NORMAL:    return "normal";
            case Mode::INHIBITED: return "inhibited";
            case Mode::SAFE:      return "safe";
            case Mode::DISABLED:  return "disabled";
            default:              return "?";
        }
    }

    /** @} */

}}  // namespace Botanik::Plc

#endif // PLC_MODE_H
