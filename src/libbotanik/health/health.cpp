/**
 * @file health.cpp
 * @brief HealthMonitor's registration: its own task, then the alarm model's.
 */

#include "libbotanik/health/health.h"     // Health::HealthMonitor

#include "libbotanik/tasks/tasks.h"       // Tasks::Registry / Tick / GroupId

namespace Botanik { namespace Health {

    /** @brief The registry hands back the owner it was given; the cast is
     *  confined to this line, beside the method it calls. */
    void HealthMonitor::_service_task(Component* c, const Tasks::Tick& t) {
        static_cast<HealthMonitor*>(c)->on_service(t.now_ms);
    }

    void HealthMonitor::register_tasks(Tasks::Registry& r) {
        r.add(*this, &HealthMonitor::_service_task, "health", Tasks::GROUP_SLOW,
              Tasks::PHASE_SERVICE);
        // Then the model, which this used to step inside its own task: same
        // order, but with a rate and a TASKS row of its own.
        _model.register_tasks(r);
    }

}}  // namespace Botanik::Health
