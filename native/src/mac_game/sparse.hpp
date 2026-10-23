#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
namespace ma::experimental::game_kernel {
template <class Key> struct SparseContents {
  std::vector<std::size_t> offsets;
  std::vector<Key> species;
  std::vector<double> mass;
  std::size_t cells() const { return offsets.empty() ? 0 : offsets.size() - 1; }
  void validate(std::size_t count) const {
    if (offsets.size() != count + 1 || offsets.front() != 0 ||
        offsets.back() != mass.size() || mass.size() != species.size())
      throw std::runtime_error("Invalid private sparse shape");
    for (std::size_t i = 0; i < count; ++i) {
      if (offsets[i] > offsets[i + 1])
        throw std::runtime_error("Invalid private sparse range");
      for (std::size_t a = offsets[i]; a < offsets[i + 1]; ++a)
        if (!(mass[a] > 0) || !std::isfinite(mass[a]) ||
            (a > offsets[i] && species[a] <= species[a - 1]))
          throw std::runtime_error("Invalid private sparse constituent");
    }
  }
};
using SparseMass = SparseContents<std::uint32_t>;
using SparsePassive = SparseContents<std::uint64_t>;
struct Contribution {
  std::size_t cell = 0;
  double fraction = 0;
};
// Merge up to five sorted source inventories without a registry-sized cell
// array. The same order is used in count and fill passes and on every worker.
template <class Emit>
void merge_mass(const SparseMass &source,
                std::array<Contribution, 5> contributions, Emit &&emit) {
  std::array<std::size_t, 5> position{}, end{};
  for (std::size_t a = 0; a < 5; ++a) {
    if (contributions[a].fraction == 0)
      continue;
    position[a] = source.offsets[contributions[a].cell];
    end[a] = source.offsets[contributions[a].cell + 1];
  }
  for (;;) {
    auto key = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t a = 0; a < 5; ++a)
      if (position[a] < end[a])
        key = std::min(key, source.species[position[a]]);
    if (key == std::numeric_limits<std::uint32_t>::max())
      break;
    double value = 0;
    for (std::size_t a = 0; a < 5; ++a)
      if (position[a] < end[a] && source.species[position[a]] == key) {
        value += contributions[a].fraction * source.mass[position[a]];
        ++position[a];
      }
    if (value > 0)
      emit(key, value);
    else if (value < 0 || !std::isfinite(value))
      throw std::runtime_error("Sparse material merge lost positivity");
  }
}
} // namespace ma::experimental::game_kernel
