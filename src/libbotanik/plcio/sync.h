#ifndef PLCIO_SYNC_H
#define PLCIO_SYNC_H

#include <cstdint>

#include "libbotanik/plcio/vars.h"
#include "libbotanik_config.h"

namespace Botanik { namespace PlcIO {

    // Reader state enumeration
    enum PlcRxState {
        RX_IDLE,                                    // wait for a new write message from the other side
        RX_READ,                                    // read data from the PLC's output shared memory
        RX_ACK                                      // acknowledge the write message from the other side
    };

    // Writer state enumeration
    enum PlcTxState {
        TX_WRITE,                                   // write data to the PLC's input shared memory
        TX_COMMIT,                                  // commit a write (or send heartbeat pulse)
        TX_WAIT,                                    // wait for the last commit to be acknowledged
        TX_IDLE                                     // wait for a new write to be requested
                                                    //  (or for the heartbeat interval to elapse)
    };

    // This class provides logic to safely interact with the PLC's shared memory,
    // even in the case where we cannot assume that the PLC runtime uses ordered
    // memory accesses to interact with the input/output shared memory ranges.
    //
    // If the PLC runtime is properly implemented, the shared memory ranges should
    // be configured to be non-cachable and to use in-order memory accesses. However,
    // sometimes the PLC runtime does not document this explicitly, and so we need 
    // this wrapper to prevent torn reads and torn writes.
    // 
    // The synchronization handler is implemented as two distinct states machines,
    // one handles reading data from the PLC's output range, and one handles
    // writing data to the PLC's input range. Both the reader and the writer use
    // explicit in-order operations and a transactional model implemented using
    // sequence/acknowledgment counters. Additionally, acknowledging/committing
    // memory operations is done in a dedicated state, so that these operations are 
    // delayed by the local cycle's period (period between calls to step()).
    //
    // The reader continuously monitors the sequence number and starts reading when
    // it becomes different from the associated acknowledgment number, while the 
    // the writer waits for writes to be requested to send a new write message to the
    // other side by copying data then incrementing its sequence number and waiting
    // for the other side to acknowledge the write.
    //
    // The synchronization handler also provides a heartbeat mechanism : after a
    // successful TX commit, when the heartbeat interval elapses, we do a 
    // "dummy write" (incrementing the sequence number without doing an actual write), 
    // so that timed_out() can return whether the other side stopped responding.
    class PlcSyncHandler {
    private:
        // NOTE: the seq/ack numbers in PlcInput_t and PlcOutput_t are named from the point of view of the PLC :
        //         - TxSeqNum/TxAckNum is RX from our point of view, as it refers to the PLC output data
        //         - RxSeqNum/RxAckNum is TX from our point of view, as it refers to the PLC input data
        uint32_t        _heartbeat_ms       = PLC_HEARTBEAT_MS;     // heartbeat commit interval
        uint32_t        _timeout_ms         = PLC_TIMEOUT_MS;       // write acknowledgement timeout delay
        uint32_t*       _timestamp_ms       = 0;                    // pointer to the current cycle's timestamp
        // Shared PLC variables
        PlcInput_t*     _plcin;                         // PLC input memory (read-write)
        PlcOutput_t*    _plcout;                        // PLC output memory (read-only)
        // Local buffers
        PlcOutputData_t _rx_buffer;                     // local receive buffer for _plcout
        PlcInputData_t  _tx_buffer;                     // local transmit buffer for _plcin
        // Local state
        PlcRxState      _rx_state           = RX_IDLE;  // reader state
        PlcTxState      _tx_state           = TX_WRITE; // writer state
        uint32_t        _tx_last_ms         = 0;        // last successful write
        bool            _tx_write           = false;    // schedule immediate write
        // TODO
        inline bool PlcSyncHandler::_rx_new_message();
        inline bool PlcSyncHandler::_tx_acknowledged();
        inline bool _tx_heartbeat_elapsed();
        // Transition actions
        inline void _rx_memcpy();
        inline void _tx_memcpy();
        inline void _rx_acknowledge();
        inline void _tx_commit();
        // Reader state machine (handle messages from the PLC's writer)
        PlcRxState _rx_step();
        // Writer state machine (send messages to the PLC's reader)
        PlcTxState _tx_step();
    public:
        // Constructor
        PlcSyncHandler(uint32_t* timestamp_ms);
        // Get a pointer to the local copy of PLC's output buffer
        const PlcOutputData_t* output();
        // Get a pointer to the local copy of PLC's input buffer
        PlcInputData_t* input();
        // Schedule a write for the next time the handler is stepped through
        void write();
        // Return whether we have timed out waiting the other side's receiver
        bool timed_out();
        // Step through the input/output synchronization state machines
        void step();
    };

}}

#endif // PLCIO_SYNC_H