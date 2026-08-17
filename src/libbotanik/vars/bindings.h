#ifndef LIBBOTANIK_VARS_BINDINGS_H
#define LIBBOTANIK_VARS_BINDINGS_H

/**
 * @file bindings.h
 * @brief ConfigBinder - the one bridge from the variable table to a component's
 *        own configuration struct.
 *
 * A component takes its settings through set_config(), and never learns where
 * they came from: with no variable model its owner fills the struct in, with one
 * this walks the table at PHASE_CONFIG and fills it from the rows. Every
 * `Vars::` mention behind a configurable component lives in bindings.cpp.
 */

#include <cstdint>

#include "libbotanik/config/vars.h"
#include "libbotanik/core/component.h"

namespace Botanik { namespace Tasks { struct Tick; } }
namespace Botanik { namespace Terminal { class Console; } }

namespace Botanik { namespace Vars {

    class Bank;

    /** @brief One component's settings, and the rows behind them.
     *
     *  @p serial names the group whose commit STAGES this binding: the settings
     *  land together on the bump rather than one per edit, so a half-typed
     *  geometry never reaches the screen. Null means unstaged - re-read every
     *  beat, for settings where a partial apply is invisible. */
    struct ConfigBinding {
        const char* serial;
        void*       target;
        void      (*apply)(void*, const Bank&, bool announce);
        uint32_t    applied;   ///< last serial pushed
        bool        ever;      ///< pushed at least once (the first push is unconditional)
    };

    /** @brief Pushes each registered component its configuration, out of the
     *  bank, at PHASE_CONFIG. */
    class ConfigBinder : public Botanik::Component {
        /** @name Table
         *  @{ */

        const Bank*   _io;
        ConfigBinding _tab[Config::CONFIG_BINDINGS_MAX];
        uint8_t       _len = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        ConfigBinder(const SetupContext& ctx, const Bank* io)
            : Component(ctx, "Config"), _io(io) {}

        /** @brief Bind @p target's configuration to @p serial's group. @p apply
         *  reads the rows and calls the component's set_config(); it is the only
         *  code that sees both sides. Silently drops past
         *  Config::CONFIG_BINDINGS_MAX. */
        void add(const char* serial, void* target,
                 void (*apply)(void*, const Bank&, bool announce));

        void register_tasks() override;

        /** @brief Push every binding whose group has moved, and every binding
         *  that has never been pushed. @warning AFTER the parameter load, or the
         *  defaults land and the stored image never does. */
        void apply_all();

        /** @} */

    private:
        static void _task(Component* c, const Tasks::Tick& t);
    };

    /** @name Bindings
     *  One per configurable component; each is the whole coupling between
     *  that component and the table. @see bindings.cpp
     *  @{ */

    void bind_console(ConfigBinder& binder, Terminal::Console& console);

    /** @} */

}}  // namespace Botanik::Vars

#endif // LIBBOTANIK_VARS_BINDINGS_H
