#ifndef BOTANIK_TESTS_RECORDING_WRITER_H
#define BOTANIK_TESTS_RECORDING_WRITER_H

#include <cstdint>
#include <cstring>
#include <string>

#include "libbotanik/logs/logger.h"

namespace tests {

    // Shared test LogWriter: records the last entry it was handed (level, topic,
    // message) and counts pushes; `loss` drives loss_count() for the Logger's
    // sink-drop reporting. Each suite reads whichever fields it asserts on.
    struct RecordingWriter : Botanik::Logs::LogWriter {
        int             pushes     = 0;
        Botanik::Logs::LogLevel last_level = Botanik::Logs::LOG_DEBUG;
        Botanik::Logs::LogTopic last_topic = Botanik::Logs::NONE;
        char            last_msg[Botanik::Config::LOG_MESSAGE_LEN] = {0};
        // Every message seen this run, newline-joined: lets a test look for a
        // line anywhere in the log rather than pinning it to last_msg.
        std::string     all_msgs;
        uint32_t        loss       = 0;
        void push(const Botanik::Logs::LogEntry* e) override {
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
        size_t format(const Botanik::Logs::LogEntry*, char*, size_t) override { return 0; }
        size_t write(const char*, size_t) override { return 0; }
    };

}  // namespace tests

#endif
