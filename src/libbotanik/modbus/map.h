#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

/**
 * @file modbus/map.h
 * @brief The register map: what a master's addresses mean in terms of the
 *        variable bank, and the type conversion in both directions.
 *
 * No transport here either - this is addresses and values. The server (server.h)
 * owns the sockets and the beat; pdu.h owns the framing; this owns the answer.
 *
 * @section map_address The address is derived, the row number is declared
 *
 * A parameter, status value or alarm carries a hand-assigned `Binding::reg`
 * (@ref dev_vars). That number is a ROW number, not an address: one register is
 * 16 bits and most rows are wider, and the declared numbers are dense (a site's
 * `cli_*` block runs 100..136, all floats), so a row cannot span `reg` and
 * `reg+1` without landing on its neighbour. Each Modbus table therefore derives
 * its own address from the same number:
 *
 * | Table | FC | Address | Holds |
 * |---|---|---|---|
 * | Holding registers | 03, 06, 16 | `2 * reg` | the value, 2 registers, high word first |
 * | Input registers | 04 | `2 * reg` | the same, read-only alias |
 * | Discrete inputs | 02 | `reg` | the row's `valid()` |
 * | Coils | 01, 05, 15 | `reg` | boolean rows; writable only if the row is a parameter |
 *
 * `2 * reg` is injective, so the site test's guarantee that no two rows share a
 * register carries over to the addresses without further checking.
 *
 * @section map_gaps Gaps read, they do not fault
 *
 * A master reads blocks, and the map is sparse (every number nobody assigned,
 * plus the odd register of every 32-bit row that only needs one). A read
 * overlapping a gap returns zeros there; only a request landing entirely outside
 * the mapped span is refused. A read that starts or ends mid-row gets that row's
 * corresponding half, so a master polling a round block never has to know where
 * the rows begin. A WRITE is the opposite: it must cover a row completely, or
 * half a 32-bit value would be committed as a whole one.
 *
 * @section map_quality Quality is a discrete input
 *
 * Modbus has no quality bit, so an invalid reading would otherwise be
 * indistinguishable from a real zero. An invalid row publishes its declared
 * substitute (`def`, which is what every other consumer sees through
 * `Var::raw()`) and reads 0 at discrete input `reg`. One FC02 block gives a
 * master the quality of everything it just read.
 */

#include <cstddef>                    // size_t - payload buffer capacities
#include <cstdint>                    // uint8_t/uint16_t/uint32_t - addresses and wire words

#include "libbotanik_config.h"        // Config::MODBUS_* - the map's fixed capacity

#include "libbotanik/modbus/pdu.h"    // Modbus::Exception - what a refusal is spelled in
#include "libbotanik/vars/model.h"    // Vars::Bank / Var / VarRef - what is being published

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
     *        from wire words.
     *
     * Built once at bring-up by scanning the bank - there is no second table to
     * keep in step, exactly as the alarm model has none. The sort is what lets a
     * lookup binary-search; the bank's own rows are in table order, which is not
     * register order (the library's rows come first and hold the highest
     * numbers).
     */
    class Map {

        /** @name State
         *  @{ */

        const Vars::Bank* _io;
        /** @brief Row indices into the bank, ascending by register. */
        uint16_t _rows[Config::MODBUS_MAX_ROWS];
        uint16_t _count;
        /** @brief Rows that did not fit MODBUS_MAX_ROWS. Non-zero means the map
         *  is silently short of what the site declared, which is exactly the
         *  kind of thing that must be reported rather than discovered. */
        uint16_t _overflow;
        /** @brief The register span actually claimed, for the "entirely outside
         *  the map" test. Both 0 when nothing is mapped. */
        uint16_t _reg_lo, _reg_hi;

        /** @} */

    public:
        explicit Map(const Vars::Bank* io = nullptr)
            : _io(io), _rows(), _count(0), _overflow(0), _reg_lo(0), _reg_hi(0) {}

        /** @name Building
         *  @{ */

        /** @brief Scan @p io (or the bank given at construction) for published
         *  rows and sort them by register. Idempotent. */
        void build(const Vars::Bank* io = nullptr);

        uint16_t count() const    { return _count; }
        uint16_t overflow() const { return _overflow; }
        /** @brief Lowest / highest register claimed by a row. */
        uint16_t reg_lo() const   { return _reg_lo; }
        uint16_t reg_hi() const   { return _reg_hi; }
        /** @brief The @p i-th mapped row, ascending by register. An index past
         *  the end, or a map with no bank, reads as an inert row rather than
         *  faulting - the same answer the bank gives a bad index. */
        Vars::Var row(uint16_t i) const;
        /** @brief Whether a row publishes at all: a parameter, a status value or
         *  an alarm, carrying a non-zero register. */
        static bool published(const Vars::Variable& d);
        /** @brief The row holding register @p reg. @p found (optional) says
         *  whether there is one; without it, an unmapped register reads as the
         *  inert row, whose name is null. Used by the server for the windows the
         *  map does not own itself - the ack block, which needs the alarm model
         *  rather than the bank. */
        Vars::Var row_at(uint16_t reg, bool* found = nullptr) const;

        /** @} */

        /** @name Reading
         *
         *  Both fill a caller-provided payload buffer in wire order, ready to
         *  hand to `encode_read()`. EX_NONE means the buffer is filled.
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
         *  Only ROLE_PARAM rows are writable, and only through `Bank::set()`, so
         *  a write over the wire gets the same band check and the same
         *  persistence an operator's does. A write whose value equals what the
         *  row already holds commits NOTHING: a master re-asserting its setpoint
         *  block every poll is normal, and a flash write per register per poll
         *  would wear the store out and blow the beat besides.
         *  @{ */

        /** @brief Apply @p count registers of @p data starting at @p addr.
         *  @param committed if non-null, receives how many rows actually changed
         *         (a no-op write reports zero, which is the point of counting). */
        Exception write_registers(uint16_t addr, uint16_t count,
                                  const uint8_t* data, Vars::Bank* io,
                                  uint16_t* committed = nullptr);
        /** @brief Apply one coil: a boolean parameter at row @p addr. */
        Exception write_bit(uint16_t addr, bool on, Vars::Bank* io,
                            uint16_t* committed = nullptr);

        /** @} */

        /** @name Conversion
         *
         *  A row's value as the 32 bits two registers carry, and back. Exposed
         *  because the tests are the specification of the wire format, and
         *  because the CSV generator (the integrator's document) must produce the
         *  same numbers the server serves.
         *  @{ */

        /** @brief @p v's value as the 32-bit word its two registers carry -
         *  override-aware, since every other reader sees the forced value too. */
        static uint32_t to_wire(const Vars::Var& v);
        /** @brief The same conversion on an explicit Scalar. What the no-op
         *  write check runs against `Var::live()`: comparing the incoming value
         *  with the OVERRIDE would skip a commit whenever a forced row happened
         *  to agree with it, and the parameter underneath would never change. */
        static uint32_t wire_of(Vars::VarType type, Vars::Scalar s);
        /** @brief The inverse: @p w as a Scalar of @p type. */
        static Vars::Scalar from_wire(Vars::VarType type, uint32_t w);

        /** @} */

    private:
        /** @name Internals
         *  @{ */
        /** @brief Index into _rows of the row holding register @p reg, or -1. */
        int _find(uint16_t reg) const;
        /** @} */
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER

#endif // MODBUS_MAP_H
