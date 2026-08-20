/**
 * @file tasks_sense.cpp
 * @brief Conditioning: the supplies, the probe zones, and the watches that
 *        judge a reading before any ladder sees it.
 *
 * The interfaces are in tasks.h; what each law does is here, so the header
 * reads as the list of programs and their variables.
 */

#include <cmath>

#include "libbotanik/utils/time.h"

#include "tasks_sense.h"

namespace Site {

using namespace Botanik;

/** @brief Filter time constant for the conditioned readings. Long enough that
 *  probe noise does not reach the ladders, short enough that the PID's tuning
 *  is not fighting it. @see the spec's note on Ziegler-Nichols. */
static const uint32_t SENSOR_TAU_MS = 30000;

// AcSupply

/** @name Publish deadbands
 *  How far a reading has to move to be worth a status write. Fixed here rather
 *  than as parameters: they are about the METER's last digit, not about the
 *  plant, and a supply an operator has to re-tune to keep quiet is a defect.
 *  @{ */

static const float SUPPLY_V_BAND  = 0.5f;
static const float SUPPLY_A_BAND  = 0.1f;
static const float SUPPLY_W_BAND  = 5.0f;
static const float SUPPLY_HZ_BAND = 0.05f;

/** @} */

void AcSupply::reset() {
    _v.reset(); _a.reset(); _w.reset(); _hz.reset();
}

void AcSupply::_mirror(Vars::RefOut<float> out, Plc::Deadband& band,
                       Vars::RefIn<float> in, float width) {
    if (in.valid()) out.set(band(in.get(), width));
    else            out.invalidate();
}

bool AcSupply::step(bool& fault) {
    // The volts and the frequency are what the verdict is made of, so those two
    // decide whether there is a verdict at all. Current and power are reported
    // either way: a meter half answering is still worth what it does answer.
    const bool measured = _in_v.valid() && _in_hz.valid();
    if (measured) _seen = true;

    const float volts = _in_v.get();
    const float hz    = _in_hz.get();

    // A mirror of a reading nobody has is not a reading: published as its
    // fallback it would read 0.0 V on a page, which is what a meter says when
    // the supply is DEAD rather than when nobody asked it. Per quantity, since
    // a meter half answering is still worth what it does answer.
    _mirror(_out_v,  _v,  _in_v,  SUPPLY_V_BAND);
    _mirror(_out_a,  _a,  _in_a,  SUPPLY_A_BAND);
    _mirror(_out_w,  _w,  _in_w,  SUPPLY_W_BAND);
    _mirror(_out_hz, _hz, _in_hz, SUPPLY_HZ_BAND);

    const bool in_spec = Plc::in_range(volts, _lo_v.get(), _hi_v.get()) &&
                         Plc::in_range(hz, _lo_hz.get(), _hi_hz.get());
    _ok.set(measured && in_spec);

    // A meter that has never answered is BRING-UP, not a supply fault: the bus
    // has not got round to it yet, and alarming on that annunciates every boot.
    // Once it has answered, silence is exactly the fault worth raising.
    fault = _seen && (!measured || !in_spec);
    return measured;
}

// AcGridWatch

void AcGridWatch::observe(ProgramContext& c) {
    if (c.first_scan) _supply.reset();
    bool fault = false;
    if (!_supply.step(fault)) degraded();
    _v.fault.set(fault);
}

// AcLocalWatch

void AcLocalWatch::observe(ProgramContext& c) {
    if (c.first_scan) { _supply.reset(); _cranking.reset(); }
    bool fault = false;
    const bool ok = _supply.step(fault);
    if (!ok) degraded();
    _v.fault.set(fault);

    // The generator only owes an answer once the grid has gone. With the grid
    // up, an unhealthy ATS output is a transfer-switch fault (alarm_ac_local
    // above) and the set was never asked to start.
    const bool waiting = !_v.grid_ok.get() && !ok;
    _v.gen_fault.set(_cranking(waiting, 1000u * _v.start_s.get(), c.dt_ms));
}

// DcWatch

bool DcWatch::_rail(const DcWatch::Rail& r, uint32_t dt_ms, bool& seen) {
    // Averaged, then deadbanded: the first says what the rail is doing, the
    // second says when that is worth a write.
    const float tau_s = 0.001f * (float)r.avg_ms.get();
    const float volts = r.avg(r.in.get(), tau_s, dt_ms);

    const bool measured = r.in.valid();
    // A rail nobody is measuring has no voltage to publish. Its LAST average
    // stays in the filter, so a divider that comes back rejoins where it left.
    if (measured) r.out.set(r.band(volts, SUPPLY_V_BAND));
    else          r.out.invalidate();
    if (measured) seen = true;
    // A divider that has never answered is the bus still coming up. One that
    // has, and has stopped, is a rail nobody can see - which is a fault.
    if (!measured) return !seen;
    return volts >= r.lo.get();
}

void DcWatch::observe(ProgramContext& c) {
    if (c.first_scan) {
        _dc1.reset(); _dc2.reset(); _ups.reset(); _gen.reset();
        _avg1.reset(); _avg2.reset(); _avgu.reset(); _avgg.reset();
        _low_for.reset();
    }

    const Rail rails[] = {
        { _avg1, _dc1, _v.dc1,     _v.out_dc1, _v.dc1_lo, _v.dc1_avg },
        { _avg2, _dc2, _v.dc2,     _v.out_dc2, _v.dc2_lo, _v.dc2_avg },
        { _avgu, _ups, _v.ups_bat, _v.out_ups, _v.ups_lo, _v.ups_avg },
        { _avgg, _gen, _v.gen_bat, _v.out_gen, _v.gen_lo, _v.gen_avg },
    };
    const bool ps1 = _rail(rails[0], c.dt_ms, _seen[0]);
    const bool ps2 = _rail(rails[1], c.dt_ms, _seen[1]);
    const bool ups = _rail(rails[2], c.dt_ms, _seen[2]);
    const bool gen = _rail(rails[3], c.dt_ms, _seen[3]);

    _v.ps1_flt.set(!ps1);
    _v.ps2_flt.set(!ps2);
    _v.gen_flt.set(!gen);

    // THE CONTACT, not the divider: the UPS knows what its own battery has
    // left, and a voltage on the rail is a guess at it. Debounced, because a
    // dry contact bounces and this one holds the whole plant.
    const bool low = _low_for(_v.low_bat.get(), 2000, c.dt_ms);
    _v.ups_flt.set(low);
    _v.lockout.set(low);

    // ...and the lockout IS the plant's safe state: every program is asked to
    // park while it stands, and released the moment it does not. Held rather
    // than requested, so it cannot cancel a halt that is still being waited on.
    if (_tasks != nullptr && low != _safe_held) {
        _safe_held = low;
        _tasks->hold_safe(low);
    }

    // The battery voltage is a WARNING and the contact is the interlock, so the
    // summary carries both: an operator reading one row wants the whole answer.
    _v.ok.set(ps1 && ps2 && ups && gen && !low);
}

// ZoneProbes

void ZoneProbes::attach(const Vars::RefIn<float>* t, const Vars::RefIn<float>* h,
                        Vars::RefOut<float> temp, Vars::RefOut<float> rh,
                        Vars::RefOut<bool> fault, float tau_s) {
    // Symmetric: a zone reading is one quantity, so rising and falling have no
    // reason to answer at different speeds.
    _tau_s = tau_s;

    for (uint8_t i = 0; i < 3; i++) {
        _t[i] = t[i];
        _h[i] = h[i];
    }
    _temp_out = temp;
    _rh_out   = rh;
    _fault    = fault;
}

ZoneVote ZoneProbes::_select(const Vars::RefIn<float>* refs) {
    float    v[3];
    uint8_t  n = 0;
    for (uint8_t i = 0; i < 3; i++) {
        // An invalid probe is EXCLUDED rather than voted on. It reads its
        // declared default, which is outside the band and would drag an average
        // even when the outlier rule dropped it.
        if (!refs[i].valid()) continue;
        v[n++] = refs[i].get();
    }
    switch (n) {
        // THE MEDIAN, per the spec: keep the middle reading and discard both
        // extremes, so a probe stuck high or stuck low can never be the one
        // retained. (The mean of the two closest would be quieter by root-two
        // and reject the same stuck probe; the spec asks for the median, and
        // the spec is what the plant was designed against.)
        case 3:  return ZoneVote{ Plc::median3(v[0], v[1], v[2]), 3 };
        // Two: no middle to keep, and their mean is the only answer there is.
        case 2:  return ZoneVote{ 0.5f * (v[0] + v[1]), 2 };
        // One: believed, because a reading from one probe still beats none.
        case 1:  return ZoneVote{ v[0], 1 };
        default: return ZoneVote{ 0.0f, 0 };
    }
}

void ZoneProbes::_publish(Vars::RefOut<float> row, Plc::Ewma& filter,
                          const ZoneVote& v, bool& seeded, float tau_s,
                          uint32_t dt_ms, float lo, float hi) {
    if (v.used == 0) {
        // Nothing believable left. The row keeps the last average this zone
        // could make and says it is not current: a reader that checks quality
        // knows not to act on it, and one that does not gets the temperature
        // the greenhouse last had rather than 0 degC, which reads like a
        // measurement of a freezing house. The filter keeps its state and
        // seeding is not re-armed, so a probe coming back rejoins the average
        // it left.
        if (seeded) row.hold(Plc::limit(filter.value(), lo, hi));
        else        row.invalidate();     // nothing has ever been measured here
        return;
    }
    seeded = true;                      // the block starts AT its first reading
    row.set(Plc::limit(filter(v.value, tau_s, dt_ms), lo, hi));
}

uint8_t ZoneProbes::step(uint32_t dt_ms) {
    // Each quantity carries its own quorum. Coupling them would let one dead
    // humidity probe invalidate a temperature three good probes agree on.
    const ZoneVote t = _select(_t);
    ZoneVote h = _select(_h);

    // The instruments report humidity in PERCENT and everything above this line
    // works in fractions - the spec's convention for a percentage is a real
    // number between 0 and 1. Converted HERE, once, at the boundary between the
    // instrument and the model: published as a percent it clamped at 1.00 and
    // every dew-point margin in the plant read zero.
    h.value *= 0.01f;

    _publish(_temp_out, _temp, t, _temp_seeded, _tau_s, dt_ms, -100.0f, 200.0f);
    _publish(_rh_out, _rh, h, _rh_seeded, _tau_s, dt_ms, 0.0f, 1.0f);

    // Two probes still select correctly, so the plant keeps running and the
    // operator is told to replace one before the second goes. The alarm is the
    // zone's, so it follows whichever quantity has lost the most.
    const uint8_t worst = (t.used < h.used) ? t.used : h.used;
    _fault.set(_quorum_for(worst < 3, 5000, dt_ms));
    return worst;
}

// TempRHZone

void TempRHZone::bind(Vars::Bank& io) {
    _v.bind(io, *this);
    const float tau_s = 0.001f * (float)SENSOR_TAU_MS;
    _ceiling.attach(_v.t_hi, _v.h_hi, _v.temp_hi, _v.rh_hi, _v.fault_hi, tau_s);
    _floor.attach(_v.t_lo, _v.h_lo, _v.temp_lo, _v.rh_lo, _v.fault_lo, tau_s);
    _crawlspace.attach(_v.t_cs, _v.h_cs, _v.temp_cs, _v.rh_cs,
                       _v.fault_cs, tau_s);
    _exterior.attach(_v.t_ext, _v.h_ext, _v.temp_ext, _v.rh_ext,
                     _v.fault_ext, tau_s);
}

void TempRHZone::run(ProgramContext& c) {
    if (c.first_scan) {
        // The filters hold a claim about the recent past, and not being called
        // means we stopped watching. Seed them again rather than resuming an
        // average of readings from before the outage.
        _ceiling.reset(); _floor.reset();
        _crawlspace.reset(); _exterior.reset();
    }

    _ceiling.step(c.dt_ms);
    _floor.step(c.dt_ms);
    _crawlspace.step(c.dt_ms);
    _exterior.step(c.dt_ms);

    const float t_hi = _ceiling.temperature();
    const float t_lo = _floor.temperature();

    // ONE interior temperature, for everything that wants the house rather than
    // a level of it. The mean of the two, or whichever one is still being
    // measured; with both gone it is not a temperature and says so.
    const bool hi_ok = _ceiling.valid();
    const bool lo_ok = _floor.valid();
    if (hi_ok && lo_ok)  _v.temp_in.set(0.5f * (t_hi + t_lo));
    else if (hi_ok)      _v.temp_in.set(t_hi);
    else if (lo_ok)      _v.temp_in.set(t_lo);
    // Both levels gone: the zones are holding their last readings, so the mean
    // of them is still the best answer there is - held, like they are.
    else                 _v.temp_in.hold(0.5f * (t_hi + t_lo));

    // Between the two interior levels the WORST case is kept, per the spec:
    // the smallest margin decides the condensation risk, and taking an average
    // would hide a level that is already at the dew point.
    const float m_hi = Plc::dew_point_margin(t_hi, _ceiling.humidity());
    const float m_lo = Plc::dew_point_margin(t_lo, _floor.humidity());
    const bool  worst_is_ceiling = (m_hi <= m_lo);
    const float t_worst  = worst_is_ceiling ? t_hi : t_lo;
    const float rh_worst = worst_is_ceiling ? _ceiling.humidity()
                                            : _floor.humidity();

    _v.dew_point.set(Plc::dew_point(t_worst, rh_worst));
    _v.dp_margin.set(worst_is_ceiling ? m_hi : m_lo);
    _v.sat_press.set(Plc::sat_vapour_pressure(t_worst));

    // The worst case is per QUANTITY, not one level chosen for all of them: the
    // spec keeps the smallest dew-point margin for rot and the LOWEST VPD for
    // irrigation, and they are not always the same level. A margin and a VPD
    // tell the same story in different units, and the units are not linear in
    // each other - the same margin is a much larger VPD when it is warm.
    _v.vpd.set(Plc::min_of(
        Plc::vapour_pressure_deficit(t_hi, _ceiling.humidity()),
        Plc::vapour_pressure_deficit(t_lo, _floor.humidity())));

    const float limit = Plc::limit(_v.rot_limit.get(), ROT_MARGIN_DEGC, 10.0f);
    _v.rot.set((worst_is_ceiling ? m_hi : m_lo) < limit);
}

// StratSensor

void StratSensor::run(ProgramContext& c) {
    // A gap computed from a level nothing is measuring is not a small error,
    // it is a number about a place. Say so instead - and FAULTED, not degraded:
    // the distinction is whether the OUTPUT is still good, and here it is gone.
    if (!_v.ceiling.valid() || !_v.floor.valid()) {
        // The levels hold their last readings, so their difference is still the
        // last profile this law saw. Held, not dropped: the fans read the
        // quality and go to their own floor, and a page reads a gap the plant
        // actually had.
        _v.strat.hold(fabsf(_v.ceiling.get() - _v.floor.get()));
        faulted();
        return;
    }
    // ABSOLUTE: destratification mixes the air whichever way the profile has
    // gone, and an inverted one - a cold roof at night over a warm floor - is
    // as much a case for the fans as the usual way round. The sign would only
    // matter to a law that wanted to move heat in one direction, and none does.
    _v.strat.set(fabsf(_v.ceiling.get() - _v.floor.get()));
}

// ClimateWatch

void ClimateWatch::observe(ProgramContext& c) {
    // Rolled up from the four zones: "a sensor is faulty" is a fact about the
    // plant that a supervisor wants without knowing there are four of them.
    bool blind = false;
    for (const Vars::RefIn<bool>& a : _v.sns) blind = blind || c.bank.get(a);
    _v.sensor.set(blind);

    const bool measured = _v.temp.valid() &&
                          _v.rh.valid();
    if (!measured) {
        // Nothing is raised on a reading nobody has. The quorum alarm above is
        // what says the plant cannot be seen, and it is already raised.
        degraded();
        _v.hot.set(false);
        _v.cold.set(false);
        _v.humid.set(false);
    } else {
        _v.hot.set(_v.temp.get() > _v.too_hot.get());
        _v.cold.set(_v.temp.get() < _v.too_cold.get());
        _v.humid.set(_v.rh.get() > _v.rh_cap.get());
    }

    // Past the band the fans are already flat out on: the gap is not something
    // destratification is going to close on its own.
    _v.stratified.set(_v.strat.valid() &&
                 _v.strat.get() > _v.strat_hi.get());
}

// WeatherWatch

void WeatherWatch::observe(ProgramContext& c) {
    const bool measured = _v.wind.valid() &&
                          _v.rain.valid();
    if (measured) _seen = true;
    // A station that has never answered is the bus still coming up; one that
    // has and has stopped is the fault the windows are already shut for.
    _v.station.set(_seen && !measured);
    if (!measured) degraded();

    // The windows' own verdict, not a second pair of thresholds beside it.
    _v.storm.set(_v.rain_lock.get() || _v.wind_lock.get());
}

// SunWatch

int16_t SunWatch::_minutes(int64_t v) {
    if (v < -1440) return -1440;
    if (v >  1440) return  1440;
    return (int16_t)v;
}

void SunWatch::observe(ProgramContext& c) {
    (void)c;
    if (!_sun.ready()) {
        // No location, or no wall clock yet - every boot passes through here
        // for as long as NTP takes. The offsets ARE the sun, so they hold
        // rather than publishing a day nobody has computed.
        _v.from_rise.hold();
        _v.from_set.hold();
        _v.elevation.hold();
        degraded();
        return;
    }

    // Signed minutes either side of the two events, which is the form every
    // boundary in the phase table is declared in. Both belong to the same solar
    // day, so one of them is always in the past and the other in the future.
    const int64_t now = (int64_t)HAL::Clock::wall_ms();
    _v.from_rise.set(_minutes((now - (int64_t)_sun.sunrise_ms()) / 60000));
    _v.from_set.set(_minutes((now - (int64_t)_sun.sunset_ms()) / 60000));

    // Degrees, because that is what a person reads. The math is in radians.
    _v.elevation.set((float)(_sun.elevation() * 57.2957795));
}

}  // namespace Site
