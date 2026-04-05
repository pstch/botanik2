#ifndef PLCIO_SHARED_VARS_H
#define PLCIO_SHARED_VARS_H

#include <cstdint>

// These definitions should match the PLC log buffer's layout
#define LOG_PLC_BUFFER_SIZE     32
#define LOG_PLC_MESSAGE_LEN     128

// Definitions injected by the Arduino PLC IDE
void* m_PLCSharedVarsInputBuf;
void *m_PLCSharedVarsOutputBuf;

#pragma pack(push, 1)
enum LogLevel : uint8_t {
    LOG_DEBUG = 1,       // debugging messages, only printed when debugging mode is enabled
    LOG_INFO = 2,        // informational messages, they don't need any particular attention
    LOG_NOTICE = 4,      // important messages, but not indicating any kind of problem
    LOG_WARNING = 8,     // important messages that indicate a potential problem
    LOG_ERROR = 16,      // error messages, something is happenning that should never happen
    LOG_CRITICAL = 32,   // critical message, something very bad is going to happen soon
};


enum LogTopic : uint8_t {
    NONE = 0,
    BOOT = 2,
    NETWORK = 3,
    TIME = 4,               
    STATUS = 5,                   // State machine's state updates
    PLC_IO = 1,            // messages about the PLC shared memory

};

struct LogEntry_t {
    uint32_t        timestamp;
    LogLevel        level;
    LogTopic        topic;
    uint16_t        length;        
    char            message[LOG_PLC_MESSAGE_LEN];
    uint16_t        seqNum;
};
 
// Input/output flags, used to synchronize the shared memory with the PLC
struct PlcInputCounters_t {
    uint32_t            rxSeqNum; // Sequence number for data sent to the PLC
    uint32_t            txAckNum; // Acknowledgement number for data received from the PLC
};
struct PlcOutputCounters_t {
    uint32_t            txSeqNum; // Sequence number for data received from the PLC
    uint32_t            rxAckNum; // Acknowledgement number for data sent to the PLC
};

// Input/output payload (should be kept in sync with the PLC's shared memory definitions)
struct PlcInputData_t {
    // PLC log read position
    uint32_t            readIndex;
};
struct PlcOutputData_t {
    // Logs received from the PLC
    LogEntry_t          logBuffer[LOG_PLC_BUFFER_SIZE];
    uint32_t            writeIndex;
};

// Input/output data (combined flags and payload)
struct PlcInput_t {
    PlcInputCounters_t  sync;
    PlcInputData_t      data;
};
struct PlcOutput_t {
    PlcOutputCounters_t sync;
    PlcOutputData_t     data;
};
#pragma pack(pop)

#ifdef __INTELLISENSE__
// In actual builds, these are automatically added by the build system.
PlcInput_t& PLCIn = (PlcInput_t&)m_PLCSharedVarsInputBuf;
PlcOutput_t& PLCOut = (PlcOutput_t&)m_PLCSharedVarsOutputBuf;
#else
// In actual builds, we use our mocked definitions to check that their layout
// matches the automatically included type definitions for the input/output structures.
static_assert(sizeof(PLCSharedVarsInput_t) == sizeof(PlcInput_t));
static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcOutput_t));
// XXX: we could improve this by also checking field offsets, but it would
//      require substantial maintenance, unless we can find some macros that
//      handles that for us (or at least make it easier to write)
//      
#endif

#endif PLCIO_SHARED_VARS_H