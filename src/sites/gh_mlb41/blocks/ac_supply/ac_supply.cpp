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
    _ok.set(measured && in_spec, true);

    // A meter that has never answered is BRING-UP, not a supply fault: the bus
    // has not got round to it yet, and alarming on that annunciates every boot.
    // Once it has answered, silence is exactly the fault worth raising.
    fault = _seen && (!measured || !in_spec);
    return measured;
}

}  // namespace Site
