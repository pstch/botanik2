#ifndef MODBUS_RTU_FRAMING_H
#define MODBUS_RTU_FRAMING_H

/**
 * @file modbus/rtu/framing.h
 * @brief Finding where a response ends, without being able to hear the silence
 *        that officially delimits it.
 *
 * @section fr_silence Why not t3.5
 *
 * Modbus RTU delimits frames by 3.5 character times of silence: 2.005 ms at
 * 19200, and a spec-fixed 1.75 ms above it. This firmware cannot observe that -
 * the framer is only LOOKED AT once per 10 ms beat, so every gap it could
 * measure has already closed. Three things replace it, and together they are
 * strictly stronger than a timer would be:
 *
 *  - @ref RtuFramer::arm() drops whatever the previous transaction left behind,
 *    so every frame begins at a known offset rather than at a guessed boundary.
 *  - The LENGTH is the delimiter. A response's size is knowable from the
 *    function code plus, for a read, its byte count - and the byte count is
 *    checked against the request before it is believed.
 *  - The CRC confirms it. A frame that ends where we thought it did, and
 *    checksums, is the frame that was sent.
 *
 * The response deadline is a backstop for a slave that says nothing at all, not
 * a frame boundary. Nothing here treats elapsed time as evidence about where a
 * frame ends.
 *
 * It takes bytes and a clock reading, and has never heard of BufferedSerial,
 * HAL::Serial, the beat or RS-485: so it is tested with array literals and a
 * fake @c now_ms, and servicing the UART more than once per beat (which the
 * budget doc wants above 38400) is a change in the caller.
 */

#include <cstdint>

#include "libbotanik/modbus/rtu/codec.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

    /**
     * @brief Accumulates one response, decides when it is complete, and parses
     * it.
     *
     * One transaction at a time, which is not a simplification: RTU is
     * half-duplex and strictly request/response, so there is never a second
     * frame in flight to accumulate.
     *
     * The usual cycle is arm() once when the request goes out, feed() whatever
     * bytes turned up on each beat, poll() to ask whether it is done, then
     * disarm() before the next transaction.
     */
    class RtuFramer {

        /** @name State
         *  @{ */

        /** @brief Where the frame accumulates. Sized to the protocol maximum, so
         *  a legal frame always fits and an over-long one is a fault rather than
         *  a reallocation. */
        uint8_t  _buf[ADU_MAX];
        /** @brief The request this is the answer to. Everything about sizing and
         *  validating the response is derived from it. */
        Expect   _exp = Expect();
        Response _resp = Response();
        /** @brief When to give up. Absolute, in the uptime-ms domain the caller
         *  passes; only ever compared, never subtracted, so a caller that skips
         *  beats cannot accumulate error. */
        uint64_t _deadline_ms = 0;

        /** @name Counters
         *  Diagnostics, and the raw material for the comms alarms a later stage
         *  raises. Separate rather than one failure count, because they point at
         *  different physical faults: see @ref Error.
         *  @{ */
        uint32_t _frames      = 0;   ///< completed, exceptions included
        uint32_t _crc_errors  = 0;   ///< cable, termination, or baud mismatch
        uint32_t _bad_frames  = 0;   ///< well-timed but wrong: unit, fc, count, echo
        uint32_t _timeouts    = 0;   ///< nothing arrived in time
        uint32_t _stray_bytes = 0;   ///< arrived when nothing was armed, or after a frame
        /** @} */

        /** @name Our own transmission
         *
         *  RE is held asserted for the life of the port (hal/platform.cpp), so
         *  the receiver hears the frame we just sent. Those bytes arrive first,
         *  they are exactly as many as we wrote, and they are not the answer.
         *  @{ */

        /** @brief Bytes still to be discarded as our own transmission. */
        uint16_t _skip     = 0;
        uint16_t _skipped  = 0;

        /** @} */

        /** @brief Bytes accumulated so far. */
        uint16_t _len  = 0;
        /** @brief The frame's total length once it is knowable; 0 until then.
         *  Doubles as the "have I worked out how long this is yet" flag. */
        uint16_t _want = 0;
        Error    _err  = ERR_OK;

        /** @brief Where the frame stands. Deliberately few: the interesting
         *  progress lives in @c _want, not in a state per header byte. */
        enum State : uint8_t {
            FR_IDLE,   ///< nothing armed; arriving bytes are strays
            FR_RX,     ///< armed, accumulating
            FR_DONE,   ///< a frame completed; see err() for OK versus exception
            FR_FAIL    ///< gave up; see err() for why
        };
        State _state = FR_IDLE;

        /** @} */

    public:
        /** @name Operation
         *  @{ */

        /** @brief Expect the answer to @p e, allowing @p timeout_ms for it.
         *
         *  Resets everything, dropping any bytes the previous transaction left
         *  unconsumed: on a shared bus a late answer from the last slave can
         *  still be arriving, and treating those bytes as the head of this frame
         *  would desynchronise every transaction after it.
         *
         *  @p echo_len is how many bytes of our own transmission to discard
         *  first: the length of the frame just sent, or 0 when nothing was. A
         *  COUNT, not a comparison - the receiver hears the wire while we drive
         *  it (RE is tied low), so the echo is our frame, whole, before anything
         *  else. A corrupted echo byte shifts the frame and the CRC rejects it,
         *  which is what a comparison would have come to as well. */
        void arm(const Expect& e, uint64_t now_ms, uint32_t timeout_ms,
                 uint16_t echo_len = 0);

        /** @brief Offer @p n bytes. @return how many were taken.
         *
         *  Stops at the end of the frame rather than swallowing the rest, so a
         *  caller that read a whole ring in one go keeps the surplus. Anything
         *  not taken is counted as stray, which is how a bus with a second master
         *  on it makes itself visible. */
        uint16_t feed(const uint8_t* b, uint16_t n);

        /** @brief Where the transaction stands, and the place the deadline is
         *  enforced.
         *
         *  @return 1 when a frame completed - ask err(), since a slave's refusal
         *  is a completed frame and a useful answer; 0 while still waiting; -1
         *  when it failed or timed out. The tri-state poll NtpClient uses. */
        int poll(uint64_t now_ms);

        /** @brief Stop expecting anything. Bytes arriving after this are strays,
         *  which is what makes a late answer countable rather than confusing. */
        void disarm();

        /** @} */

        /** @name Result
         *  @{ */

        /** @brief The parsed response. Valid only while poll() reads 1, and its
         *  @c data points into this framer's own buffer, so it dies at the next
         *  arm(). */
        const Response& response() const { return _resp; }
        /** @brief Why the last frame ended as it did. ERR_EXCEPTION here means a
         *  healthy slave that refused, not a broken one. */
        Error err() const { return _err; }
        bool  armed() const { return _state == FR_RX; }
        /** @brief Bytes accumulated so far, for a diagnostic that wants to say
         *  how far a truncated frame got. */
        uint16_t len() const { return _len; }

        /** @} */

        /** @name Counters
         *  @{ */

        uint32_t frames() const      { return _frames; }
        uint32_t crc_errors() const  { return _crc_errors; }
        uint32_t bad_frames() const  { return _bad_frames; }
        uint32_t timeouts() const    { return _timeouts; }
        uint32_t stray_bytes() const { return _stray_bytes; }
        void reset_counters();

        /** @} */

    private:
        /** @brief Take one byte into the frame, resolving and completing as the
         *  shape becomes knowable. The one path bytes reach @c _buf by. */
        void _accept(uint8_t b);
        /** @brief Work out the frame's total length, if enough of the header has
         *  arrived to say. Sets @c _want, or fails the frame when the byte count
         *  contradicts the request. */
        void _resolve();
        /** @brief Parse the accumulated frame and settle into DONE or FAIL. */
        void _complete();
        /** @brief Give up with @p e. */
        void _fail(Error e);
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_FRAMING_H
