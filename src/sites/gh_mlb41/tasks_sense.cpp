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

#include "tasks.h"

namespace Site {

using namespace Botanik;

/** @brief Filter time constant for the conditioned readings. Long enough that
 *  probe noise does not reach the ladders, short enough that the PID's tuning
 *  is not fighting it. @see the spec's note on Ziegler-Nichols. */
static const uint32_t SENSOR_TAU_MS = 30000;

/** @brief How often a zone runs, and so the sample period its fixed blend
 *  factor is derived from. GROUP_FAST, stated here because ema_alpha() needs
 *  the period as a number rather than as a rate class. */
static const uint32_t ZONE_PERIOD_MS = Config::TASK_PERIOD_MS;

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

bool AcSupply::step(Vars::Bank& io, bool& fault) {
    // The volts and the frequency are what the verdict is made of, so those two
    // decide whether there is a verdict at all. Current and power are reported
    // either way: a meter half answering is still worth what it does answer.
    const bool measured = io.var(_in_v.r).valid() && io.var(_in_hz.r).valid();
    if (measured) _seen = true;

    const float volts = io.get(_in_v);
    const float hz    = io.get(_in_hz);
    io.drive(_out_v,  _v(volts, SUPPLY_V_BAND));
    io.drive(_out_a,  _a(io.get(_in_a), SUPPLY_A_BAND));
    io.drive(_out_w,  _w(io.get(_in_w), SUPPLY_W_BAND));
    io.drive(_out_hz, _hz(hz, SUPPLY_HZ_BAND));

    const bool in_spec = Plc::in_range(volts, io.get(_lo_v), io.get(_hi_v)) &&
                         Plc::in_range(hz, io.get(_lo_hz), io.get(_hi_hz));
    io.drive(_ok, measured && in_spec);

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
    if (!_supply.step(c.bank, fault)) degraded();
    c.bank.drive(_v.fault, fault);
}

// AcLocalWatch

void AcLocalWatch::observe(ProgramContext& c) {
    if (c.first_scan) { _supply.reset(); _cranking.reset(); }
    bool fault = false;
    const bool ok = _supply.step(c.bank, fault);
    if (!ok) degraded();
    c.bank.drive(_v.fault, fault);

    // The generator only owes an answer once the grid has gone. With the grid
    // up, an unhealthy ATS output is a transfer-switch fault (alarm_ac_local
    // above) and the set was never asked to start.
    const bool waiting = !c.bank.get(_v.grid_ok) && !ok;
    c.bank.drive(_v.gen_fault,
                 _cranking(waiting, 1000u * c.bank.get(_v.start_s), c.dt_ms));
}

// DcWatch

bool DcWatch::_rail(Vars::Bank& io, const Rail& r, uint32_t dt_ms, bool& seen) {
    // Averaged, then deadbanded: the first says what the rail is doing, the
    // second says when that is worth a write.
    const float volts = r.avg(io.get(r.in), 0.001f * io.get(r.avg_ms), dt_ms);
    io.drive(r.out, r.band(volts, SUPPLY_V_BAND));

    const bool measured = io.var(r.in.r).valid();
    if (measured) seen = true;
    // A divider that has never answered is the bus still coming up. One that
    // has, and has stopped, is a rail nobody can see - which is a fault.
    if (!measured) return !seen;
    return volts >= io.get(r.lo);
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
    const bool ps1 = _rail(c.bank, rails[0], c.dt_ms, _seen[0]);
    const bool ps2 = _rail(c.bank, rails[1], c.dt_ms, _seen[1]);
    const bool ups = _rail(c.bank, rails[2], c.dt_ms, _seen[2]);
    const bool gen = _rail(c.bank, rails[3], c.dt_ms, _seen[3]);

    c.bank.drive(_v.ps1_flt, !ps1);
    c.bank.drive(_v.ps2_flt, !ps2);
    c.bank.drive(_v.gen_flt, !gen);

    // THE CONTACT, not the divider: the UPS knows what its own battery has
    // left, and a voltage on the rail is a guess at it. Debounced, because a
    // dry contact bounces and this one holds the whole plant.
    const bool low = _low_for(c.bank.get(_v.low_bat), 2000, c.dt_ms);
    c.bank.drive(_v.ups_flt, low);
    c.bank.drive(_v.lockout, low);

    // The battery voltage is a WARNING and the contact is the interlock, so the
    // summary carries both: an operator reading one row wants the whole answer.
    c.bank.drive(_v.ok, ps1 && ps2 && ups && gen && !low);
}

// ZoneProbes

void ZoneProbes::attach(const Vars::RefIn<float>* t, const Vars::RefIn<float>* h,
                        Vars::RefOut<float> temp, Vars::RefOut<float> rh,
                        Vars::RefOut<bool> fault, float alpha) {
    // Symmetric: a zone reading is one quantity, so rising and falling have no
    // reason to answer at different speeds.
    _temp.configure(alpha, alpha);
    _rh.configure(alpha, alpha);

    for (uint8_t i = 0; i < 3; i++) {
        _t[i] = t[i];
        _h[i] = h[i];
    }
    _temp_out = temp;
    _rh_out   = rh;
    _fault    = fault;
}

ZoneVote ZoneProbes::_select(const Vars::Bank& io, const Vars::RefIn<float>* refs) {
    float    v[3];
    uint8_t  n = 0;
    for (uint8_t i = 0; i < 3; i++) {
        // An invalid probe is EXCLUDED rather than voted on. It reads its
        // declared default, which is outside the band and would drag an average
        // even when the outlier rule dropped it.
        if (!io.var(refs[i].r).valid()) continue;
        v[n++] = io.get(refs[i]);
    }
    switch (n) {
        // Three agreeing probes: drop the outlier, average the pair that agree.
        case 3:  return ZoneVote{ Plc::closest2_mean(v[0], v[1], v[2]), 3 };
        // Two: they ARE the agreeing pair; there is no third to call an outlier.
        case 2:  return ZoneVote{ 0.5f * (v[0] + v[1]), 2 };
        // One: believed, because a reading from one probe still beats none.
        case 1:  return ZoneVote{ v[0], 1 };
        default: return ZoneVote{ 0.0f, 0 };
    }
}

void ZoneProbes::_publish(Vars::Bank& io, Vars::RefOut<float> row,
                          Utils::ExpMovingAverage& filter, const ZoneVote& v,
                          bool& seeded, float lo, float hi) {
    if (v.used == 0) {
        // Nothing believable left. Marking the row invalid is the whole action:
        // Var::_under() falls back to the DECLARED DEFAULT for any quality that
        // is not GOOD or STALE, so the row reads its default from here rather
        // than holding a filtered number nothing is measuring. The filter keeps
        // its state, and seeding is not re-armed - a probe coming back rejoins
        // the average it left.
        io.var(row.r).invalidate(Vars::Q_NO_DATA);
        return;
    }
    if (!seeded) {                      // start AT the reading, not at zero
        filter.preset(v.value);
        seeded = true;
    }
    filter.push(v.value);
    io.drive(row, Plc::limit(filter.value(), lo, hi));
}

uint8_t ZoneProbes::step(Vars::Bank& io, uint32_t dt_ms) {
    // Each quantity carries its own quorum. Coupling them would let one dead
    // humidity probe invalidate a temperature three good probes agree on.
    const ZoneVote t = _select(io, _t);
    const ZoneVote h = _select(io, _h);

    _publish(io, _temp_out, _temp, t, _temp_seeded, -100.0f, 200.0f);
    _publish(io, _rh_out, _rh, h, _rh_seeded, 0.0f, 1.0f);

    // Two probes still select correctly, so the plant keeps running and the
    // operator is told to replace one before the second goes. The alarm is the
    // zone's, so it follows whichever quantity has lost the most.
    const uint8_t worst = (t.used < h.used) ? t.used : h.used;
    io.drive(_fault, _quorum_for(worst < 3, 5000, dt_ms));
    return worst;
}

// TempRHZone

void TempRHZone::bind(Vars::Bank& io) {
    _v.bind(io, *this);
    const float alpha = Utils::ema_alpha(SENSOR_TAU_MS, ZONE_PERIOD_MS);
    _ceiling.attach(_v.t_hi, _v.h_hi, _v.temp_hi, _v.rh_hi, _v.fault_hi, alpha);
    _floor.attach(_v.t_lo, _v.h_lo, _v.temp_lo, _v.rh_lo, _v.fault_lo, alpha);
    _crawlspace.attach(_v.t_cs, _v.h_cs, _v.temp_cs, _v.rh_cs, _v.fault_cs, alpha);
    _exterior.attach(_v.t_ext, _v.h_ext, _v.temp_ext, _v.rh_ext, _v.fault_ext, alpha);
}

void TempRHZone::run(ProgramContext& c) {
    if (c.first_scan) {
        // The filters hold a claim about the recent past, and not being called
        // means we stopped watching. Seed them again rather than resuming an
        // average of readings from before the outage.
        _ceiling.reset(); _floor.reset(); _crawlspace.reset(); _exterior.reset();
    }

    _ceiling.step(c.bank, c.dt_ms);
    _floor.step(c.bank, c.dt_ms);
    _crawlspace.step(c.bank, c.dt_ms);
    _exterior.step(c.bank, c.dt_ms);

    const float t_hi = _ceiling.temperature(c.bank);
    const float t_lo = _floor.temperature(c.bank);

    // Between the two interior levels the WORST case is kept, per the spec:
    // the smallest margin decides the condensation risk, and taking an average
    // would hide a level that is already at the dew point.
    const float m_hi = Plc::dew_point_margin(t_hi, _ceiling.humidity(c.bank));
    const float m_lo = Plc::dew_point_margin(t_lo, _floor.humidity(c.bank));
    const bool  worst_is_ceiling = (m_hi <= m_lo);
    const float t_worst  = worst_is_ceiling ? t_hi : t_lo;
    const float rh_worst = worst_is_ceiling ? _ceiling.humidity(c.bank)
                                            : _floor.humidity(c.bank);

    c.bank.drive(_v.dew_point, Plc::dew_point(t_worst, rh_worst));
    c.bank.drive(_v.dp_margin, worst_is_ceiling ? m_hi : m_lo);
    c.bank.drive(_v.sat_press, Plc::sat_vapour_pressure(t_worst));
    c.bank.drive(_v.vpd, Plc::vapour_pressure_deficit(t_worst, rh_worst));

    const float limit = Plc::limit(c.bank.get(_v.rot_limit), ROT_MARGIN_DEGC, 10.0f);
    c.bank.drive(_v.rot, (worst_is_ceiling ? m_hi : m_lo) < limit);
}

// StratSensor

void StratSensor::run(ProgramContext& c) {
    // A gap computed from a level nothing is measuring is not a small error,
    // it is a number about a place. Say so instead - and FAULTED, not degraded:
    // the distinction is whether the OUTPUT is still good, and here it is gone.
    if (!c.bank.var(_v.ceiling.r).valid() || !c.bank.var(_v.floor.r).valid()) {
        c.bank.var(_v.strat.r).invalidate(Vars::Q_NO_DATA);
        faulted();
        return;
    }
    // ABSOLUTE: destratification mixes the air whichever way the profile has
    // gone, and an inverted one - a cold roof at night over a warm floor - is
    // as much a case for the fans as the usual way round. The sign would only
    // matter to a law that wanted to move heat in one direction, and none does.
    c.bank.drive(_v.strat, fabsf(c.bank.get(_v.ceiling) - c.bank.get(_v.floor)));
}

// ClimateWatch

void ClimateWatch::observe(ProgramContext& c) {
    // Rolled up from the four zones: "a sensor is faulty" is a fact about the
    // plant that a supervisor wants without knowing there are four of them.
    bool blind = false;
    for (const Vars::RefIn<bool>& a : _v.sns) blind = blind || c.bank.get(a);
    c.bank.drive(_v.sensor, blind);

    const bool measured = c.bank.var(_v.temp.r).valid() &&
                          c.bank.var(_v.rh.r).valid();
    if (!measured) {
        // Nothing is raised on a reading nobody has. The quorum alarm above is
        // what says the plant cannot be seen, and it is already raised.
        degraded();
        c.bank.drive(_v.hot, false);
        c.bank.drive(_v.cold, false);
        c.bank.drive(_v.humid, false);
    } else {
        c.bank.drive(_v.hot, c.bank.get(_v.temp) > c.bank.get(_v.too_hot));
        c.bank.drive(_v.cold, c.bank.get(_v.temp) < c.bank.get(_v.too_cold));
        c.bank.drive(_v.humid, c.bank.get(_v.rh) > c.bank.get(_v.rh_cap));
    }

    // Past the band the fans are already flat out on: the gap is not something
    // destratification is going to close on its own.
    c.bank.drive(_v.stratified,
                 c.bank.var(_v.strat.r).valid() &&
                 c.bank.get(_v.strat) > c.bank.get(_v.strat_hi));
}

// WeatherWatch

void WeatherWatch::observe(ProgramContext& c) {
    const bool measured = c.bank.var(_v.wind.r).valid() &&
                          c.bank.var(_v.rain.r).valid();
    if (measured) _seen = true;
    // A station that has never answered is the bus still coming up; one that
    // has and has stopped is the fault the windows are already shut for.
    c.bank.drive(_v.station, _seen && !measured);
    if (!measured) degraded();

    // The windows' own verdict, not a second pair of thresholds beside it.
    c.bank.drive(_v.storm,
                 c.bank.get(_v.rain_lock) || c.bank.get(_v.wind_lock));
}

}  // namespace Site
