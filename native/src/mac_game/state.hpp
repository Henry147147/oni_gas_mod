#pragma once
#include "sparse.hpp"
namespace ma::experimental::game_kernel {
struct State {
  SparseMass masses;
  std::vector<double> energy, jx, jy;
  SparsePassive passives;
};
} // namespace ma::experimental::game_kernel
