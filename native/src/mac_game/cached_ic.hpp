#pragma once
#include "../mac_kernel_detail.hpp"
#include <numeric>
namespace ma::experimental::game_kernel {
// Exact structural cache for the existing component IC solve. Numeric matrix
// entries and IC factors are rebuilt at every solve, in the original order.
class CachedComponents {
  struct Edge {
    std::size_t left, right, face;
    bool x;
  };
  struct Boundary {
    std::size_t node, face;
    bool x;
  };
  struct Part {
    std::vector<std::size_t> cells;
    std::vector<Edge> faces;
    std::vector<Boundary> boundary;
  };
  std::vector<Part> parts;
  std::vector<std::uint8_t> active, nonzero_x, nonzero_y;
  std::size_t rebuilds_ = 0;
  template <class G>
  void prepare(const G &g,
               const ma::experimental::mac_kernel::LinearSystem &sys) {
    const auto same = [](const auto &weights, const auto &old) {
      if (weights.size() != old.size())
        return false;
      for (std::size_t f = 0; f < weights.size(); ++f)
        if ((weights[f] != 0) != static_cast<bool>(old[f]))
          return false;
      return true;
    };
    if (active == sys.active && same(sys.wx, nonzero_x) &&
        same(sys.wy, nonzero_y))
      return;
    ++rebuilds_;
    active = sys.active;
    nonzero_x.resize(sys.wx.size());
    nonzero_y.resize(sys.wy.size());
    for (std::size_t f = 0; f < sys.wx.size(); ++f)
      nonzero_x[f] = sys.wx[f] != 0;
    for (std::size_t f = 0; f < sys.wy.size(); ++f)
      nonzero_y[f] = sys.wy[f] != 0;
    const auto N = g.N;
    std::vector<std::size_t> nodes(N, N), cells;
    for (std::size_t i = 0; i < N; ++i)
      if (sys.active[i]) {
        nodes[i] = cells.size();
        cells.push_back(i);
      }
    std::vector<Edge> edges;
    std::vector<Boundary> boundaries;
    const auto face = [&](bool x, std::size_t f, auto l, auto r,
                          double weight) {
      if (weight == 0)
        return;
      const auto a = l == ma::experimental::mac_kernel::none ? N : nodes[l],
                 b = r == ma::experimental::mac_kernel::none ? N : nodes[r];
      if (a != N && b != N)
        edges.push_back({a, b, f, x});
      else if (a != N)
        boundaries.push_back({a, f, x});
      else if (b != N)
        boundaries.push_back({b, f, x});
    };
    for (std::size_t f = 0; f < g.NX; ++f)
      face(true, f, g.xl[f], g.xr[f], sys.wx[f]);
    for (std::size_t f = 0; f < g.NY; ++f)
      face(false, f, g.yl[f], g.yr[f], sys.wy[f]);
    const auto M = cells.size();
    std::vector<std::size_t> parent(M), labels(M, M), which(M), local(M);
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t i) {
      while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
      }
      return i;
    };
    for (const auto &e : edges) {
      const auto a = root(e.left), b = root(e.right);
      if (a != b)
        parent[b] = a;
    }
    parts.clear();
    for (std::size_t i = 0; i < M; ++i) {
      const auto r = root(i);
      if (labels[r] == M) {
        labels[r] = parts.size();
        parts.emplace_back();
      }
      which[i] = labels[r];
      auto &part = parts[which[i]];
      local[i] = part.cells.size();
      part.cells.push_back(cells[i]);
    }
    for (const auto &e : edges)
      parts[which[e.left]].faces.push_back(
          {local[e.left], local[e.right], e.face, e.x});
    for (const auto &e : boundaries)
      parts[which[e.node]].boundary.push_back({local[e.node], e.face, e.x});
  }

public:
  std::size_t rebuilds() const { return rebuilds_; }
  ma::experimental::mac_kernel::LinearSolveResult
  solve(ma::experimental::mac_kernel::Context &c,
        const ma::experimental::mac_kernel::LinearSystem &sys,
        const std::vector<double> &b, std::vector<double> &x, double rtol,
        double atol, std::size_t maximum) {
    namespace k = ma::experimental::mac_kernel;
    prepare(c.g, sys);
    x.assign(c.g.N, 0);
    struct Result {
      k::LinearSolveResult linear;
      bool ic = false, fallback = false;
    };
    std::vector<Result> results(parts.size());
    const double local_atol =
        parts.empty()
            ? atol
            : .5 * atol / std::sqrt(static_cast<double>(parts.size()));
    c.exec.pool.run(parts.size(), [&](std::size_t index) {
      const auto &part = parts[index];
      std::vector<double> diagonal, rhs;
      std::vector<ma::PressureFace> faces;
      diagonal.reserve(part.cells.size());
      rhs.reserve(part.cells.size());
      faces.reserve(part.faces.size());
      for (const auto cell : part.cells) {
        diagonal.push_back(sys.diag[cell]);
        rhs.push_back(b[cell]);
      }
      for (const auto &boundary : part.boundary)
        diagonal[boundary.node] +=
            (boundary.x ? sys.wx : sys.wy)[boundary.face];
      for (const auto &edge : part.faces)
        faces.push_back(
            {edge.left, edge.right, (edge.x ? sys.wx : sys.wy)[edge.face]});
      ma::PressureSystem matrix(
          std::move(diagonal), std::move(faces),
          ma::PressurePreconditioner::incomplete_cholesky);
      const auto solved = matrix.solve(rhs, std::vector<double>(rhs.size(), 0),
                                       .5 * rtol, local_atol, maximum);
      for (std::size_t j = 0; j < part.cells.size(); ++j)
        x[part.cells[j]] = solved.pressure[j];
      results[index] = {{solved.iterations, solved.residual_norm, true},
                        solved.preconditioner_used ==
                            ma::PressurePreconditioner::incomplete_cholesky,
                        solved.preconditioner_fallback};
    });
    k::LinearSolveResult result;
    result.converged = true;
    k::DD squared;
    for (const auto &solved : results) {
      result.iterations += solved.linear.iterations;
      squared.add(solved.linear.residual * solved.linear.residual);
      c.counters.incomplete_cholesky_solves += solved.ic ? 1 : 0;
      c.counters.incomplete_cholesky_fallbacks += solved.fallback ? 1 : 0;
    }
    c.counters.maximum_linear_components =
        std::max(c.counters.maximum_linear_components, parts.size());
    result.residual = std::sqrt(squared.value());
    return result;
  }
};
} // namespace ma::experimental::game_kernel
