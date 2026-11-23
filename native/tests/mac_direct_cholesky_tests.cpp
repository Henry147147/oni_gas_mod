// Sparse Cholesky of game pressure systems (mac_direct_cholesky.hpp) against
// a dense long double factorisation of the same matrices: grid systems with
// walls, a corridor, separate parts and a single cell; new values on a kept
// pattern and a changed pattern; bit-identical repeats; and refusal of an
// indefinite, an exactly singular and a non-finite system.
#include "mac_direct_cholesky.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace direct = ma::experimental::mac_kernel::direct;
void check(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
// A fixed sequence (64-bit LCG): every run builds the same systems.
struct Sequence {
  std::uint64_t state;
  double next() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
  }
};
// A pressure system on a W x H grid: open cells (walls false), positive
// compliance on every cell, a positive weight on each face between two open
// cells unless `cut` drops it. A = diag(compliance + sum w) - w off the
// diagonal, as solve_components_cached assembles it.
struct System {
  std::size_t W = 0, H = 0;
  std::vector<std::uint8_t> walls;
  direct::Graph graph;
  std::vector<std::size_t> cells;      // node -> cell
  std::vector<double> compliance, weight_x, weight_y, diagonal, off, rhs;
};
System build(std::size_t W, std::size_t H, const std::vector<std::uint8_t> &walls, Sequence &random, double contrast,
             const std::vector<std::uint8_t> *cut_x = nullptr) {
  System s;
  s.W = W; s.H = H; s.walls = walls;
  s.weight_x.assign((W + 1) * H, 0);
  s.weight_y.assign(W * (H + 1), 0);
  std::vector<std::size_t> node(W * H, direct::absent);
  for (std::size_t i = 0; i < W * H; ++i)
    if (!walls[i]) { node[i] = s.cells.size(); s.cells.push_back(i); }
  for (std::size_t y = 0; y < H; ++y)
    for (std::size_t x = 1; x < W; ++x)
      if (!walls[y * W + x - 1] && !walls[y * W + x] && !(cut_x && (*cut_x)[y * (W + 1) + x]))
        s.weight_x[y * (W + 1) + x] = std::pow(contrast, random.next());
  for (std::size_t y = 1; y < H; ++y)
    for (std::size_t x = 0; x < W; ++x)
      if (!walls[(y - 1) * W + x] && !walls[y * W + x]) s.weight_y[y * W + x] = std::pow(contrast, random.next());
  s.graph.offsets.assign(1, 0);
  for (const auto i : s.cells) {
    const auto x = i % W, y = i / W;
    const double c = 1e-3 * (.5 + random.next());
    double d = c;
    const auto add = [&](std::size_t other, double w) {
      d += w;
      if (w == 0) return;
      s.graph.columns.push_back(node[other]);
      s.off.push_back(-w);
    };
    if (x > 0) add(i - 1, s.weight_x[y * (W + 1) + x]);
    if (x + 1 < W) add(i + 1, s.weight_x[y * (W + 1) + x + 1]);
    if (y > 0) add(i - W, s.weight_y[y * W + x]);
    if (y + 1 < H) add(i + W, s.weight_y[(y + 1) * W + x]);
    s.compliance.push_back(c);
    s.diagonal.push_back(d);
    s.graph.offsets.push_back(s.graph.columns.size());
    s.graph.at.push_back({x, y});
    s.rhs.push_back(random.next() - .5);
  }
  return s;
}
// x = A^-1 b by a dense long double Cholesky.
std::vector<long double> dense_solve(const System &s) {
  const auto n = s.cells.size();
  std::vector<long double> a(n * n, 0), x(s.rhs.begin(), s.rhs.end());
  for (std::size_t k = 0; k < n; ++k) {
    a[k * n + k] = s.diagonal[k];
    for (auto p = s.graph.offsets[k]; p < s.graph.offsets[k + 1]; ++p) a[k * n + s.graph.columns[p]] = s.off[p];
  }
  for (std::size_t j = 0; j < n; ++j) {
    for (std::size_t k = 0; k < j; ++k) a[j * n + j] -= a[j * n + k] * a[j * n + k];
    check(a[j * n + j] > 0, "Dense reference lost positivity");
    a[j * n + j] = std::sqrt(a[j * n + j]);
    for (std::size_t i = j + 1; i < n; ++i) {
      for (std::size_t k = 0; k < j; ++k) a[i * n + j] -= a[i * n + k] * a[j * n + k];
      a[i * n + j] /= a[j * n + j];
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t k = 0; k < i; ++k) x[i] -= a[i * n + k] * x[k];
    x[i] /= a[i * n + i];
  }
  for (std::size_t i = n; i-- > 0;) {
    for (std::size_t k = i + 1; k < n; ++k) x[i] -= a[k * n + i] * x[k];
    x[i] /= a[i * n + i];
  }
  return x;
}
struct Solved {
  std::vector<double> x, Lx;
  double error = 0, residual = 0;
};
Solved solve(const System &s, const direct::Symbolic &symbolic) {
  direct::Factor factor;
  check(direct::factor(symbolic, s.graph, s.diagonal, s.off, factor), "A positive definite system was refused");
  // The numeric fill must be the symbolic pattern: every column full.
  for (std::size_t j = 0; j < symbolic.n; ++j)
    check(factor.fill[j] == symbolic.Lp[j + 1] - symbolic.Lp[j], "The factor's fill differs from its symbolic pattern");
  Solved out;
  std::vector<double> work;
  direct::solve(symbolic, factor, s.rhs, out.x, work);
  out.Lx = factor.Lx;
  const auto reference = dense_solve(s);
  // Normwise backward error |b - A x| / (|A| |x| + |b|) (maximum norms).
  long double scale = 0, error = 0, residual = 0, row = 0, x_norm = 0, b_norm = 0;
  for (std::size_t k = 0; k < out.x.size(); ++k) {
    scale = std::max(scale, std::abs(reference[k]));
    error = std::max(error, std::abs(reference[k] - out.x[k]));
    long double r = s.rhs[k] - static_cast<long double>(s.diagonal[k]) * out.x[k], sum = std::abs(s.diagonal[k]);
    for (auto p = s.graph.offsets[k]; p < s.graph.offsets[k + 1]; ++p) {
      r -= static_cast<long double>(s.off[p]) * out.x[s.graph.columns[p]];
      sum += std::abs(s.off[p]);
    }
    residual = std::max(residual, std::abs(r));
    row = std::max(row, sum);
    x_norm = std::max(x_norm, static_cast<long double>(std::abs(out.x[k])));
    b_norm = std::max(b_norm, static_cast<long double>(std::abs(s.rhs[k])));
  }
  out.error = static_cast<double>(error / scale);
  out.residual = static_cast<double>(residual / (row * x_norm + b_norm));
  return out;
}
std::vector<std::uint8_t> frame(std::size_t W, std::size_t H) {
  std::vector<std::uint8_t> walls(W * H, 0);
  return walls;
}
} // namespace

int main() {
  try {
    Sequence random{20261123};
    // Shapes: an open room, a cave with walls inside, a corridor one cell
    // wide, two separate rooms (a forest of elimination trees) and a cell.
    std::vector<std::pair<std::string, System>> systems;
    {
      const auto open = frame(13, 9);
      systems.emplace_back("open room", build(13, 9, open, random, 1e3));
    }
    {
      auto cave = frame(17, 12);
      for (std::size_t i = 0; i < cave.size(); ++i) {
        const auto x = i % 17, y = i / 17;
        cave[i] = (x % 5 == 2 && y % 4 != 1) || (y == 6 && x > 3 && x < 14) || random.next() < .08;
      }
      systems.emplace_back("cave", build(17, 12, cave, random, 1e5));
    }
    {
      auto corridor = frame(30, 3);
      for (std::size_t i = 0; i < corridor.size(); ++i) corridor[i] = i / 30 != 1;
      systems.emplace_back("corridor", build(30, 3, corridor, random, 1e2));
    }
    {
      auto two = frame(11, 6);
      for (std::size_t i = 0; i < two.size(); ++i) two[i] = i % 11 == 5;
      systems.emplace_back("two rooms", build(11, 6, two, random, 1e4));
    }
    systems.emplace_back("single cell", build(1, 1, frame(1, 1), random, 1));
    double worst_error = 0, worst_residual = 0;
    for (const auto &[name, s] : systems) {
      const auto order = direct::nested_dissection(s.graph);
      std::vector<std::uint8_t> seen(order.size(), 0);
      for (const auto k : order) {
        check(k < seen.size() && !seen[k], name + ": the ordering is not a permutation");
        seen[k] = 1;
      }
      check(order.size() == s.cells.size(), name + ": the ordering lost a node");
      const auto symbolic = direct::analyse(s.graph, order);
      const auto first = solve(s, symbolic), again = solve(s, symbolic);
      check(first.x == again.x && first.Lx == again.Lx, name + ": a repeated factorisation differs");
      // The backward error is the rounding-level check; the forward error
      // follows the conditioning (contrasts up to 1e5 over compliance 1e-3).
      check(first.error <= 1e-8, name + ": the solution differs from the dense reference");
      check(first.residual <= 64 * 2.220446049250313e-16, name + ": the backward error is not at rounding level");
      worst_error = std::max(worst_error, first.error);
      worst_residual = std::max(worst_residual, first.residual);
    }
    // New values on a kept pattern, then a changed pattern (a face dropped):
    // the kept analysis serves the first, a new analysis the second.
    {
      auto cave = frame(15, 10);
      for (std::size_t i = 0; i < cave.size(); ++i) cave[i] = (i % 15 == 7 && i / 15 != 4) || (i / 15 == 5 && i % 15 < 4);
      Sequence a{7}, b{7};
      auto first = build(15, 10, cave, a, 1e4);
      const auto symbolic = direct::analyse(first.graph, direct::nested_dissection(first.graph));
      check(solve(first, symbolic).error <= 1e-8, "The first values were solved wrongly");
      for (auto &value : first.diagonal) value *= 1.5;
      for (auto &value : first.off) value *= .75;
      for (auto &value : first.rhs) value = -value;
      check(solve(first, symbolic).error <= 1e-8, "New values on the kept pattern were solved wrongly");
      std::vector<std::uint8_t> cut((15 + 1) * 10, 0);
      cut[4 * 16 + 7] = cut[4 * 16 + 8] = 1; // close the passage through the inner wall
      const auto changed = build(15, 10, cave, b, 1e4, &cut);
      check(changed.graph.columns.size() + 4 == first.graph.columns.size(), "The changed pattern did not drop two faces");
      const auto renewed = direct::analyse(changed.graph, direct::nested_dissection(changed.graph));
      check(solve(changed, renewed).error <= 1e-8, "A changed pattern was solved wrongly");
    }
    // Refusals: an indefinite system, an exactly singular one (two cells and
    // one face, no compliance) and one holding a non-finite value.
    {
      direct::Graph pair;
      pair.offsets = {0, 1, 2};
      pair.columns = {1, 0};
      pair.at = {{0, 0}, {1, 0}};
      const auto symbolic = direct::analyse(pair, direct::nested_dissection(pair));
      direct::Factor factor;
      check(!direct::factor(symbolic, pair, {-1., 2.}, {-.5, -.5}, factor), "An indefinite system was factored");
      check(!direct::factor(symbolic, pair, {.25, .25}, {-.25, -.25}, factor), "A singular system was factored");
      check(!direct::factor(symbolic, pair, {1., std::nan("")}, {-.5, -.5}, factor), "A non-finite system was factored");
      check(direct::factor(symbolic, pair, {1., 1.}, {-.5, -.5}, factor), "A positive definite pair was refused");
    }
    std::cout << "PASS sparse Cholesky of pressure systems: " << systems.size()
              << " shapes against a dense long double factorisation (largest relative error " << worst_error
              << ", backward error " << worst_residual << "), new values on a kept pattern, a changed pattern, "
              << "bit-identical repeats, and indefinite, singular and non-finite systems refused\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
