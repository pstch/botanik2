#ifndef PLC_FUNCTIONS_PSYCHRO_H
#define PLC_FUNCTIONS_PSYCHRO_H

/**
 * @file psychro.h
 * @brief Moist-air arithmetic: dew point, dew-point margin, vapour pressure and
 *        VPD. What "how wet is it" means to a control law.
 * @note IEC 61131-3 FUNCTIONs, like the rest of this directory: no state, no dt.
 *       Not constexpr - logf and expf are not, in C++11.
 * @note Derivations and which quantity governs what: docs/dev/psychro.md.
 */

#include <cmath>

#include "libbotanik/plc/functions/math.h"

namespace Botanik { namespace Plc {

    /** @name Magnus coefficients
     *  Over water, the pairing this file uses throughout. Two SETS exist in the
     *  literature and they are not interchangeable; mixing one set's constants
     *  with another's shifts the dew point by a few tenths of a degree.
     *  @{ */
    static const float MAGNUS_A = 17.62f;
    static const float MAGNUS_B = 243.12f;   ///< degC
    /** @} */

    /** @name Tetens coefficients
     *  Saturation vapour pressure in kPa for a temperature in degC.
     *  @{ */
    static const float TETENS_K = 0.6108f;   ///< kPa
    static const float TETENS_A = 17.27f;
    static const float TETENS_B = 237.3f;    ///< degC
    /** @} */

    /** @brief The floor a relative humidity is held to before the logarithm.
     *  ln(0) is undefined, so a sensor reading 0 %RH would otherwise produce a
     *  NaN that propagates into every law downstream. 5 %RH is far below
     *  anything a greenhouse sees, so the clamp never fires on real air. */
    static const float RH_FLOOR = 0.05f;

    /** @brief Magnus's gamma for temperature @p t_degC and relative humidity
     *  @p rh (a FRACTION, 0..1, never a percentage). Intermediate only.
     *  @note Valid for -40..+50 degC. */
    inline float magnus_gamma(float t_degC, float rh) {
        return logf(limit(rh, RH_FLOOR, 1.0f))
             + (MAGNUS_A * t_degC) / (MAGNUS_B + t_degC);
    }

    /** @brief Dew point in degC: the temperature at which this air saturates
     *  and its vapour begins condensing onto whatever it touches. */
    inline float dew_point(float t_degC, float rh) {
        const float g = magnus_gamma(t_degC, rh);
        return (MAGNUS_B * g) / (MAGNUS_A - g);
    }

    /** @brief Dew-point margin (depression) in degC: how far the air is from
     *  condensing. LARGER is drier.
     *
     *  This is what the rot and condensation laws are set in, because it is the
     *  margin that decides whether water lands on a plant at the temperature it
     *  is actually at. */
    inline float dew_point_margin(float t_degC, float rh) {
        return t_degC - dew_point(t_degC, rh);
    }

    /** @brief Saturation vapour pressure in kPa at @p t_degC (Tetens): the most
     *  water this air could hold. */
    inline float sat_vapour_pressure(float t_degC) {
        return TETENS_K * expf((TETENS_A * t_degC) / (t_degC + TETENS_B));
    }

    /** @brief Vapour-pressure deficit in kPa: how much more water this air could
     *  take. LARGER is thirstier, so it draws harder on plant and substrate.
     *
     *  The same fact as the dew-point margin in a different unit, and the
     *  relation is not linear - a 3 degC margin is about 0.4 kPa at 10 degC but
     *  near 1.0 kPa at 30 degC. Irrigation is set in kPa because the deficit is
     *  what tracks the WATER moved; rot is set in degC. */
    inline float vapour_pressure_deficit(float t_degC, float rh) {
        return sat_vapour_pressure(t_degC)
             - sat_vapour_pressure(dew_point(t_degC, rh));
    }

}}  // namespace Botanik::Plc

#endif // PLC_FUNCTIONS_PSYCHRO_H
