#pragma once
#include <algorithm>
#include "liquid.hpp"
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace ma::experimental::game_kernel {
constexpr double R = 8.31446261815324;
struct Species {
  bool gas = true, positive_liquid = false;
  std::uint32_t liquid_group = 0;
  double molar_mass = .03, cv = 700, offset = 0, rho0 = 1000, stiffness = 1e8,
         conductivity = 0, entropy_offset = 0;
  double minimum_temperature = 1, maximum_temperature = 1e6;
};
inline double liquid_volume_fraction(const Species &s, double p) {
  return s.positive_liquid ? positive_liquid::volume_fraction(p, s.stiffness)
                           : 1 - p / s.stiffness;
}
inline double liquid_specific_elastic(const Species &s, double p) {
  return s.positive_liquid ? positive_liquid::specific_elastic(p, s.stiffness, s.rho0)
                           : .5 * p * p / (s.rho0 * s.stiffness);
}
struct Eos {
  bool fixed_pressure = false;
  double mass = 0, capacity = 0, offset = 0, nR = 0;
  double liquid_nominal_volume = 0, liquid_compliance = 0;
  double pressure = 0, temperature = 0, gas_volume = 0, liquid_volume = 0,
         elastic_energy = 0;
  double relative_closure = 0;
};
// Provisional constant-density liquid model for the private prototype.
// Liquid volume is V0*(1-p/K); elastic energy is V0*p^2/(2*K).
// The shared-temperature mixture closure is quadratic, including elastic U.
// Thermal expansion is pending catalogue clarification. No state is clipped.
// Sets `result` and returns null, or returns the message of the domain
// error evaluate_selected throws for this input. Trial searches use it to
// reject a state without unwinding.
template <class Key, class Mass>
inline const char *try_evaluate_selected(Eos &result,
                                         std::span<const Species> species,
                                         std::size_t count, Key key, Mass mass,
                                         double energy, double volume) {
  if (!(volume > 0) || !std::isfinite(energy))
    return "Invalid prototype EOS input";
  result = Eos{};
  double minimum = 0, maximum = 1e300;
  for (std::size_t a = 0; a < count; ++a) {
    const auto k = key(a);
    const double m = mass(a);
    if (!(m >= 0) || !std::isfinite(m))
      return "Invalid prototype mass";
    if (m == 0)
      continue;
    const auto &s = species[k];
    result.mass += m;
    result.capacity += m * s.cv;
    result.offset += m * s.offset;
    minimum = std::max(minimum, s.minimum_temperature);
    maximum = std::min(maximum, s.maximum_temperature);
    if (s.gas)
      result.nR += m * R / s.molar_mass;
    else {
      const double nominal = m / s.rho0;
      result.liquid_nominal_volume += nominal;
      result.liquid_compliance += nominal / s.stiffness;
    }
  }
  if (result.mass == 0)
    return energy != 0 ? "Empty prototype cell retains energy" : nullptr;
  if (!(result.capacity > 0))
    return "Invalid prototype heat capacity";
  const double thermal_elastic = energy - result.offset;
  if (!(thermal_elastic > 0))
    return "Prototype has no positive thermal energy";
  if (result.nR > 0) {
    const double ratio = result.nR / result.capacity;
    const double a = result.liquid_compliance * (1 + .5 * ratio);
    const double b = volume - result.liquid_nominal_volume;
    const double c = ratio * thermal_elastic;
    if (a == 0)
      result.pressure = c / volume;
    else {
      const double discriminant =
          std::hypot(b, 2 * std::sqrt(a) * std::sqrt(c));
      result.pressure =
          b >= 0 ? 2 * c / (b + discriminant) : (discriminant - b) / (2 * a);
    }
  } else if (result.liquid_nominal_volume > volume) {
    result.pressure =
        (result.liquid_nominal_volume - volume) / result.liquid_compliance;
  }
  bool extended = false;
  for (std::size_t j=0;j<count;++j) if(mass(j)>0&&!species[key(j)].gas&&species[key(j)].positive_liquid)
    extended = extended || result.pressure >= .5*species[key(j)].stiffness;
  if (extended) {
    const auto each=[&](auto emit){for(std::size_t j=0;j<count;++j)if(mass(j)>0&&!species[key(j)].gas&&species[key(j)].positive_liquid)emit(mass(j)/species[key(j)].rho0,species[key(j)].stiffness);};
    const auto root=positive_liquid::pressure(volume,result.nR/result.capacity,thermal_elastic,0,false,result.pressure,each);
    result.pressure=root.pressure;
    result.liquid_compliance=root.coefficients.c;
    result.liquid_volume=root.coefficients.b-root.coefficients.c*result.pressure;
    result.elastic_energy=.5*root.coefficients.c*result.pressure*result.pressure+root.coefficients.d;
  } else {
    result.elastic_energy=.5*result.liquid_compliance*result.pressure*result.pressure;
    result.liquid_volume=result.liquid_nominal_volume-result.liquid_compliance*result.pressure;
  }

  result.temperature =
      (thermal_elastic - result.elastic_energy) / result.capacity;

  if (result.nR > 0)
    result.gas_volume = result.nR * result.temperature / result.pressure;
  if (!(result.temperature >= minimum && result.temperature <= maximum) ||
      !(result.pressure >= 0) || !std::isfinite(result.pressure))
    return "Prototype EOS outside its thermal/pressure domain";
  for (std::size_t a = 0; a < count; ++a)
    if (mass(a) > 0 && !species[key(a)].gas &&
        !(species[key(a)].positive_liquid ? liquid_volume_fraction(species[key(a)],result.pressure)>0 : result.pressure < species[key(a)].stiffness))
      return "Prototype linear liquid strain is too large";
  if (result.nR > 0)
    result.relative_closure =
        std::abs(result.gas_volume + result.liquid_volume - volume) / volume;
  return nullptr;
}
template <class Key, class Mass>
inline Eos evaluate_selected(std::span<const Species> species,
                             std::size_t count, Key key, Mass mass,
                             double energy, double volume) {
  Eos result;
  if (const auto error = try_evaluate_selected(result, species, count, key,
                                               mass, energy, volume))
    throw std::runtime_error(error);
  return result;
}
inline Eos evaluate(std::span<const Species> species,
                    std::span<const double> masses, double energy,
                    double volume) {
  if (species.size() != masses.size())
    throw std::runtime_error("Invalid prototype EOS input");
  return evaluate_selected(
      species, masses.size(), [](std::size_t k) { return k; },
      [&](std::size_t k) { return masses[k]; }, energy, volume);
}
} // namespace ma::experimental::game_kernel
