#ifndef MODBUS_TCP_MAP_H
#define MODBUS_TCP_MAP_H

/**
 * @file modbus/tcp/map.h
 * @brief The register map: what a master's addresses mean in terms of the
 *        variable bank, and the type conversion in both directions.
 * @note No transport here: tcp/slave.h owns the sockets and the beat, pdu.h the
 *       framing, this the answer.
 * @note The address derivation, the sparse-gap rule and the quality table are
 *       the integrator's reference: docs/dev/modbus.md, "The TCP slave".
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik/config/modbus.h"

#include "libbotanik/modbus/tcp/pdu.h"
#include "libbotanik/vars/access.h"
#include "libbotanik/vars/model.h"

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

namespace Botanik { namespace Modbus {

    /** @brief Which bit table a bit request is addressing. Both are indexed by
     *  the row number itself; they differ in what the bit MEANS. */
    enum BitTable {
        BITS_VALUE,     ///< coils: the row's boolean value
        BITS_QUALITY    ///< discrete inputs: the row's valid()
    };

    /**
     * @brief The published rows, sorted by register, with the conversion to and
     *        from wire words. Built once at bring-up by scanning the bank.
     * @note The sort is what lets a lookup binary-search: the bank's rows are in
     *       table order, which is not register order.
     */
    class Map {

        /** @name State
         *  @{ */

        Vars::Bank* _io;
        uint16_t _rows[Config::MODBUS_TCP_MAX_ROWS];  ///< bank indices, ascending by register
        uint16_t _count;
        uint16_t _overflow;       ///< rows that did not fit; non-zero must be reported
        uint16_t _reg_lo, _reg_hi;  ///< span claimed, for the "outside the map" test; 0/0 if empty

        /** @name The persist queue
         *
         *  Rows a master has written whose flash copy has not been made yet.
         *  Storage is synchronous, so committing a full request's worth inside
         *  the beat that decoded it would let a remote peer stall the firmware.
         *  @{ */
        uint16_t     _pending[Config::MODBUS_PERSIST_QUEUE];
        uint16_t     _pending_head  = 0;
        uint16_t     _pending_count = 0;
        /** @} */

        /** @} */

    public:
        explicit Map(Vars::Bank* io = nullptr)
            : _io(io), _rows(), _count(0), _overflow(0), _reg_lo(0), _reg_hi(0) {}

        /** @name Building
         *  @{ */

        /** @brief Scan the bank given at construction for published rows and sort
         *  them by register. Idempotent. */
        void build();

        uint16_t count() const    { return _count; }
        uint16_t overflow() const { return _overflow; }

        /** @name Persisting, paced
         *  @{ */

        /** @brief Write at most @p n queued rows to the store, once per beat. A
         *  TDBStore write is a record append plus a page program, and a garbage
         *  collection adds a sector erase: all 61 rows of a full FC16 in one beat
         *  would run past the field bus's own response deadline.
         *  @return how many were written. */
        uint16_t drain_persists(uint16_t n);

        /** @brief How many rows are still owed a flash write. */
        uint16_t pending_persists() const { return _pending_count; }

        /** @brief Room left in the queue. A request needing more than this is
         *  refused whole with EX_SLAVE_BUSY rather than half applied. */
        uint16_t persist_room() const {
            return (uint16_t)(Config::MODBUS_PERSIST_QUEUE - _pending_count);
        }

        /** @} */
        /** @brief The @p i-th mapped row, ascending by register. Past the end, or
         *  a map with no bank, reads as an inert row rather than faulting. */
        Vars::Var row(uint16_t i) const;

        /** @brief Position of the row holding register @p reg in this map's own
         *  order, or -1. Integer-only, so a caller resolving names once at
         *  bring-up has something to key a parallel array on. */
        int index_of(uint16_t reg) const { return _find(reg); }

        /** @brief Whether a row publishes at all: a parameter, a status value or
         *  an alarm, carrying a non-zero register. */
        static bool published(const Vars::Descriptor& d);

    private:
        /** @brief Queue row @p r for a flash write. False when the queue is full,
         *  which the caller must have ruled out already. */
        bool _queue_persist(uint16_t r);
    public:
        /** @brief The row holding register @p reg.
         *  @param found optional; without it an unmapped register reads as the
         *         inert row, whose name is null. */
        Vars::Var row_at(uint16_t reg, bool* found = nullptr) const;

        /** @} */

        /** @name Reading
         *
         *  Both fill a caller-provided payload buffer in wire order, ready for
         *  encode_read(). EX_NONE means the buffer is filled.
         *  @{ */

        /** @brief @p count registers from @p addr into @p out (2 bytes each). */
        Exception read_registers(uint16_t addr, uint16_t count, uint8_t* out,
                                 size_t cap) const;
        /** @brief @p count bits from @p addr into @p out, packed LSB first. */
        Exception read_bits(uint16_t addr, uint16_t count, BitTable table,
                            uint8_t* out, size_t cap) const;

        /** @} */

        /** @name Writing
         *
         *  Only ROLE_PARAM rows are writable, and only through Bank::set(), so a
         *  write over the wire gets the same band check and persistence an
         *  operator's does. A write whose value equals what the row already holds
         *  commits NOTHING: a master re-asserting its setpoint block every poll is
         *  normal, and a flash write per register per poll would wear the store out.
         *  @{ */

        /** @brief Apply the single register @p addr, splicing it into the other
         *  half of its row, then validating and committing the whole row.
         *  @see docs/dev/modbus.md for why FC06 is supported rather than refused. */
        Exception write_register(uint16_t addr, uint16_t value,
                                 uint16_t* committed = nullptr);

        /** @brief Apply @p count registers of @p data starting at @p addr.
         *  @param committed if non-null, receives how many rows actually changed
         *         (a no-op write reports zero, which is the point of counting). */
        Exception write_registers(uint16_t addr, uint16_t count,
                                  const uint8_t* data,
                                  uint16_t* committed = nullptr);
        /** @brief Apply one coil: a boolean parameter at row @p addr. */
        Exception write_bit(uint16_t addr, bool on,
                            uint16_t* committed = nullptr);

        /** @} */

        /** @name Conversion
         *
         *  A row's value as the 32 bits two registers carry, and back. Exposed so
         *  the tests can specify the wire format and the CSV generator produces
         *  the numbers the server serves.
         *  @{ */

        /** @brief @p v's value as the 32-bit word its two registers carry -
         *  override-aware, since every other reader sees the forced value too. */
        static uint32_t to_wire(const Vars::Var& v);
        /** @brief The same conversion on an explicit Scalar.
         *  @note The no-op write check runs it against Var::live(): comparing
         *  against an OVERRIDE would skip the commit whenever a forced row agreed,
         *  leaving the parameter underneath unchanged. */
        static uint32_t wire_of(Vars::VarType type, Vars::Scalar s);
        /** @brief The inverse: @p w as a Scalar of @p type. */
        static Vars::Scalar from_wire(Vars::VarType type, uint32_t w);

        /** @} */

    private:
        /** @name Internals
         *  @{ */
        /** @brief Index into _rows of the row holding register @p reg, or -1. */
        int _find(uint16_t reg) const;
        /** @brief Index of the first row at or past register @p reg (count() when
         *  there is none). What every request walks from: the map is SPARSE, so a
         *  block read is mostly gaps, and one lower bound plus a cursor replaces a
         *  binary search per register. */
        uint16_t _lower_bound(uint16_t reg) const;
        /** @brief Register of the @p i-th mapped row; 0xFFFF past the end, so a
         *  walk can compare against it without a bounds test. */
        uint16_t _reg_of(uint16_t i) const;
        /** @} */
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER

#endif // MODBUS_TCP_MAP_H
