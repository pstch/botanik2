#ifndef BOTANIK_TESTS_RECORDING_WRITER_H
#define BOTANIK_TESTS_RECORDING_WRITER_H

#include <cstdint>
#include <cstring>

#include "libbotanik/logs/logger.h"

namespace Botanik {

    // Shared test LogWriter: records the last entry it was handed (level, topic,
    // message) and counts pushes; `loss` drives loss_count() for the Logger's
    // sink-drop reporting. Each suite reads whichever fields it asserts on.
    struct RecordingWriter : Logs::LogWriter {
        int             pushes     = 0;
        Logs::LogLevel last_level = Logs::LOG_DEBUG;
        Logs::LogTopic last_topic = Logs::NONE;
        char            last_msg[Config::LOG_PLC_MESSAGE_LEN] = {0};
        uint32_t        loss       = 0;
        void push(const PlcIO::LogEntry_t* e) override {
            pushes++; last_level = e->level; last_topic = e->topic;
            memcpy(last_msg, e->message, sizeof(last_msg));
        }
        void drain() override {}
        uint32_t loss_count() const override { return loss; }
        size_t format(const PlcIO::LogEntry_t*, char*, size_t) override { return 0; }
        size_t write(const char*, size_t) override { return 0; }
    };

}

#endif
