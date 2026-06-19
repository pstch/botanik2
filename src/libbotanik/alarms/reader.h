#ifndef ALARMS_READER_H
#define ALARMS_READER_H

/**
 * @file reader.h
 * @brief AlarmReader - reads the PLC alarm field and drives the model each loop.
 *
 * The loop-facing coordinator for alarms (the counterpart of PlcLogReader for the
 * log ring): each step() reads the raw alarm bitset from the site PLC output
 * payload, hands it to the AlarmModel, and the model dispatches the due ALARM-topic
 * log lines and MQTT notifications. Runtime-sourced (local) alarms are set on the
 * model directly via set_local(); the reader only carries the remote field.
 *
 * Templated on the site payload, which must expose a 64-bit `alarms` bitset member
 * (same contract the StatusPage payload hooks rely on).
 */

#include <cstdint>

#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Alarms {

    /** @brief Reads the PLC alarm field and steps the model (see file header). */
    template<typename OutputData>
    class AlarmReader {
    private:
        /** @name State
         *  @{ */
        AlarmModel*       _model;   // analysis + dispatch (may be null)
        const OutputData* _data;    // PLC output payload (local snapshot; may be null)
        /** @} */

    public:
        /** @brief Bind to the alarm model and the PLC output payload. */
        AlarmReader(AlarmModel* model, const OutputData* data)
            : _model(model), _data(data) {}

        /** @brief Read the remote alarm bitset and advance the model, dispatching any
         *  due log lines / notifications. @p now is the current uptime in ms. */
        void step(uint64_t now) {
            uint64_t raw = (_data != nullptr) ? _data->alarms : 0;
            if (_model != nullptr) _model->step(raw, now);
        }

        /** @brief The model this reader drives (for queries / set_local). */
        AlarmModel* model() const { return _model; }
    };

}}

#endif // ALARMS_READER_H
