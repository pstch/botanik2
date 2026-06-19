#ifndef ALERTS_READER_H
#define ALERTS_READER_H

/**
 * @file reader.h
 * @brief AlertReader - reads the PLC alert field and drives the model each loop.
 *
 * The loop-facing coordinator for alerts (the counterpart of PlcLogReader for the
 * log ring): each step() reads the raw alert bitset from the site PLC output
 * payload, hands it to the AlertModel, and the model dispatches the due ALERT-topic
 * log lines and MQTT notifications. Runtime-sourced (local) alerts are set on the
 * model directly via set_local(); the reader only carries the remote field.
 *
 * Templated on the site payload, which must expose a 64-bit `alerts` bitset member
 * (same contract the StatusPage payload hooks rely on).
 */

#include <cstdint>

#include "libbotanik/alerts/alerts.h"

namespace Botanik { namespace Alerts {

    /** @brief Reads the PLC alert field and steps the model (see file header). */
    template<typename OutputData>
    class AlertReader {
    private:
        /** @name State
         *  @{ */
        AlertModel*       _model;   // analysis + dispatch (may be null)
        const OutputData* _data;    // PLC output payload (local snapshot; may be null)
        /** @} */

    public:
        /** @brief Bind to the alert model and the PLC output payload. */
        AlertReader(AlertModel* model, const OutputData* data)
            : _model(model), _data(data) {}

        /** @brief Read the remote alert bitset and advance the model, dispatching any
         *  due log lines / notifications. @p now is the current uptime in ms. */
        void step(uint64_t now) {
            uint64_t raw = (_data != nullptr) ? _data->alerts : 0;
            if (_model != nullptr) _model->step(raw, now);
        }

        /** @brief The model this reader drives (for queries / set_local). */
        AlertModel* model() const { return _model; }
    };

}}

#endif // ALERTS_READER_H
