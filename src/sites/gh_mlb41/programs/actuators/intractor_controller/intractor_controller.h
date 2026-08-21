#ifndef SITE_PROGRAM_INTRACTOR_CONTROLLER_H
#define SITE_PROGRAM_INTRACTOR_CONTROLLER_H

/**
 * @file intractor_controller.h
 * @brief HAND-WRITTEN - two variable-speed fans, driven by whichever demand wants the most.
 */

#include <cstdint>

#include "../../../vars.h"
#include "intractor_controller.gen.h"

namespace Site {

using namespace Botanik;

class IntractorController final : public Gen::IntractorController {

public:
    IntractorController(const SetupContext& ctx, Vars::Bank& io)
        : Gen::IntractorController(ctx, io) {}

    /** @brief How many intractors the crawlspace has. */
    static const uint8_t INTRACTORS = 2;

protected:
    void run(ProgramContext& c) override;

private:
    /** @brief Where these go when this law is not acting, off the row's own
     *  declared safe value. NOT stopped: some air is always wanted. */
    float _safe_speed() const;
};

}  // namespace Site

#endif // SITE_PROGRAM_INTRACTOR_CONTROLLER_H
