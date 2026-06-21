#ifndef TESTS_PLC_CONFIG_FIXTURE_H
#define TESTS_PLC_CONFIG_FIXTURE_H

/**
 * @file plc_config_fixture.h
 * @brief Test helper: drive a real PlcSyncHandler over throwaway shared regions so a
 *        test can populate the PLC->host config block and read it back through
 *        PlcSyncContext::config() - the exact path the live components consume.
 *
 * Empty payloads keep it independent of any site (the conf block lives in the
 * PlcOutput<> wrapper, not the Data payload). Set fields via conf(), then publish()
 * pushes them through one RX transaction so config() reflects them.
 */

#include <cstdint>

#include "libbotanik/plcio/sync.h"

namespace botanik_test {

    struct PlcConfigFixture {
        struct In {};
        struct Out {};

        Botanik::PlcIO::PlcInput<In>            in{};
        Botanik::PlcIO::PlcOutput<Out>          out{};
        uint32_t                                ts = 0;
        Botanik::PlcIO::PlcSyncHandler<In, Out> handler{&in, &out, &ts, nullptr};

        /** @brief The config the mock PLC publishes. Set fields, then call publish(). */
        Botanik::PlcIO::PlcOutputConfig_t& conf() { return out.conf; }

        /** @brief Push the current conf into the handler's RX snapshot via one PLC->host
         *  transaction: bump the PLC sequence, then run the RX machine through
         *  IDLE -> READ (copies conf) -> ACK. */
        void publish() {
            out.sync.txSeqNum += 1;               // signal a fresh PLC message
            for (int i = 0; i < 4; ++i) handler.step();
        }

        /** @brief The populated context to hand a component under test. */
        Botanik::PlcIO::PlcSyncContext* ctx() { return handler.context(); }
    };

}  // namespace botanik_test

#endif // TESTS_PLC_CONFIG_FIXTURE_H
