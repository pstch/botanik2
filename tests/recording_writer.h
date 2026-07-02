#ifndef BOTANIK_TESTS_RECORDING_WRITER_H
#define BOTANIK_TESTS_RECORDING_WRITER_H

#include <cstdint>
#include <cstring>
#include <string>

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
        // Every message seen this run, newline-joined. Lets a test look for a
        // line anywhere in the log instead of pinning it to last_msg, which is
        // fragile when the order/count of emitted lines is timing-dependent.
        std::string     all_msgs;
        uint32_t        loss       = 0;
        void push(const PlcIO::LogEntry_t* e) override {
            pushes++; last_level = e->level; last_topic = e->topic;
            memcpy(last_msg, e->message, sizeof(last_msg));
            all_msgs.append(e->message); all_msgs.push_back('\n');
        }
        // True if any recorded message contains @p needle.
        bool saw(const char* needle) const {
            return all_msgs.find(needle) != std::string::npos;
        }
        void drain() override {}
        uint32_t loss_count() const override { return loss; }
        size_t format(const PlcIO::LogEntry_t*, char*, size_t) override { return 0; }
        size_t write(const char*, size_t) override { return 0; }
    };

}

#endif
