/**
 * @file ac_supply.cpp
 * @brief HAND-WRITTEN - the supply block, out of line.
 */

#include "ac_supply.h"

namespace Site {

void AcSupply::reset() {
    _v.reset(); _a.reset(); _w.reset(); _hz.reset();
}

void AcSupply::_mirror(Vars::RefOut<float> out, Plc::Deadband& band,
                       Vars::RefIn<float> in, float width) {
    if (in.valid()) out.set(band(in.get(), width), true);
    else            out.invalidate();
}

bool AcSupply::operator()() {
    // The volts and the frequency are what the VERDICT is made of, so those two
    // decide whether there is a verdict at all. Current and power are reported
    // either way: a meter half answering is still worth what it does answer.
    const bool measured = _r.volts.valid() && _r.hz.valid();
    if (measured) _seen = true;
    // But each mirror below is its own network of one input, and one that is
    // out is a network this block is not producing. So the verdict and the
    // block being WHOLE are two questions, and this is the second.
    const bool whole = measured && _r.amps.valid() && _r.watts.valid();

    const float volts = _r.volts.get();
    const float hz    = _r.hz.get();

    // A mirror of a reading nobody has is not a reading: published as its
    // fallback it would read 0.0 V on a page, which is what a meter says when
    // the supply is DEAD rather than when nobody asked it. Per quantity, since
    // a meter half answering is still worth what it does answer.
    _mirror(_r.out_V,  _v,  _r.volts,  SUPPLY_V_BAND);
    _mirror(_r.out_A,  _a,  _r.amps,  SUPPLY_A_BAND);
    _mirror(_r.out_W,  _w,  _r.watts,  SUPPLY_W_BAND);
    _mirror(_r.out_Hz, _hz, _r.hz, SUPPLY_HZ_BAND);

    const bool in_spec = Plc::in_range(volts, _r.lo_V.get(), _r.hi_V.get()) &&
                         Plc::in_range(hz, _r.lo_Hz.get(), _r.hi_Hz.get());
    _r.ok.set(measured && in_spec, true);

    // A meter that has never answered is BRING-UP, not a supply fault: the bus
    // has not got round to it yet, and alarming on that annunciates every boot.
    // Once it has answered, silence is exactly the fault worth raising.
    _r.fault.set(_seen && (!measured || !in_spec), true);
    return whole;
}

}  // namespace Site
