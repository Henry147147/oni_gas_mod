#include "mac_pressure_multigrid.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace ma::experimental::interval_detail {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
constexpr std::size_t dense_limit = 24;
void require(bool ok, const char* reason) { if (!ok) throw std::invalid_argument(reason); }
void adjacency(MultigridLevel& level) {
    const auto n = level.coordinates.size(); level.adjacent_offsets.assign(n + 1, 0);
    for (const auto& edge : level.edges) { ++level.adjacent_offsets[edge.left + 1]; ++level.adjacent_offsets[edge.right + 1]; }
    std::partial_sum(level.adjacent_offsets.begin(), level.adjacent_offsets.end(), level.adjacent_offsets.begin());
    auto at = level.adjacent_offsets; level.adjacent.resize(2 * level.edges.size());
    for (std::size_t e = 0; e < level.edges.size(); ++e) {
        const auto& edge = level.edges[e]; level.adjacent[at[edge.left]++] = {edge.right, e}; level.adjacent[at[edge.right]++] = {edge.left, e};
    }
}
std::size_t span(std::size_t coordinate, std::size_t stride, std::size_t extent) {
    return std::min(stride, extent - coordinate * stride);
}
double centre(std::size_t coordinate, std::size_t stride, std::size_t extent) {
    return static_cast<double>(coordinate * stride) + .5 * static_cast<double>(span(coordinate, stride, extent));
}
std::size_t neighbor(const MultigridLevel& level, const MacGrid& grid, std::size_t node, std::size_t axis, int sign) {
    const auto extent = axis == 0 ? grid.width : grid.height;
    const auto count = (extent + level.stride - 1) / level.stride, coordinate = level.coordinates[node][axis];
    const bool periodic = axis == 0 ? grid.periodic_x : grid.periodic_y;
    if (count == 1 || (!periodic && (sign < 0 ? coordinate == 0 : coordinate + 1 == count))) return none;
    const auto target = sign < 0 ? (coordinate + count - 1) % count : (coordinate + 1) % count;
    auto result = none;
    for (auto k = level.adjacent_offsets[node]; k < level.adjacent_offsets[node + 1]; ++k) {
        const auto& candidate = level.adjacent[k];
        if (level.edges[candidate.edge].axis != axis || level.coordinates[candidate.node][axis] != target) continue;
        // Ambiguous pore branches use constant interpolation locally. This
        // changes only the preconditioner, never a physical face/operator.
        if (result != none && result != candidate.node) return none;
        result = candidate.node;
    }
    return result;
}
std::pair<std::size_t, double> interpolation(const MultigridLevel& fine, const MultigridLevel& coarse,
    const MacGrid& grid, std::size_t node, std::size_t parent, std::size_t axis) {
    const auto extent = axis == 0 ? grid.width : grid.height;
    const double position = centre(fine.coordinates[node][axis], fine.stride, extent);
    const double origin = centre(coarse.coordinates[parent][axis], coarse.stride, extent), displacement = position - origin;
    if (displacement == 0) return {none, 0};
    const int sign = displacement < 0 ? -1 : 1;
    const auto target = neighbor(coarse, grid, parent, axis, sign); if (target == none) return {none, 0};
    double next = centre(coarse.coordinates[target][axis], coarse.stride, extent);
    if (sign < 0 && next >= origin) next -= static_cast<double>(extent);
    if (sign > 0 && next <= origin) next += static_cast<double>(extent);
    const double weight = displacement / (next - origin);
    require(std::isfinite(weight) && weight > 0 && weight < 1, "Invalid multigrid interpolation geometry");
    return {target, weight};
}
void transfers(MultigridLevel& fine, const MultigridLevel& coarse, const MacGrid& grid) {
    fine.prolong_offsets.reserve(fine.coordinates.size() + 1); fine.prolong_offsets.push_back(0);
    for (std::size_t i = 0; i < fine.coordinates.size(); ++i) {
        const auto p = fine.parent[i]; const auto [nx, wx] = interpolation(fine, coarse, grid, i, p, 0);
        std::array<MultigridTerm, 2> horizontal{{{p, 1 - wx}, {nx, wx}}};
        std::array<MultigridTerm, 4> row{}; std::size_t used = 0;
        for (const auto& term : horizontal) if (term.node != none && term.weight > 0) {
            const auto [ny, wy] = interpolation(fine, coarse, grid, i, term.node, 1);
            row[used++] = {term.node, term.weight * (1 - wy)};
            if (ny != none && wy > 0) row[used++] = {ny, term.weight * wy};
        }
        // Stable insertion order also fixes the summation of duplicate terms.
        for (std::size_t k = 1; k < used; ++k) {
            const auto value = row[k]; auto j = k;
            while (j > 0 && value.node < row[j - 1].node) { row[j] = row[j - 1]; --j; }
            row[j] = value;
        }
        for (std::size_t k = 0; k < used; ++k) {
            if (fine.prolong.size() > fine.prolong_offsets.back() && fine.prolong.back().node == row[k].node)
                fine.prolong.back().weight += row[k].weight;
            else fine.prolong.push_back(row[k]);
        }
        fine.prolong_offsets.push_back(fine.prolong.size());
    }
    fine.restrict_offsets.assign(coarse.coordinates.size() + 1, 0);
    for (const auto& term : fine.prolong) ++fine.restrict_offsets[term.node + 1];
    std::partial_sum(fine.restrict_offsets.begin(), fine.restrict_offsets.end(), fine.restrict_offsets.begin());
    auto at = fine.restrict_offsets; fine.restrict.resize(fine.prolong.size());
    for (std::size_t i = 0; i < fine.coordinates.size(); ++i)
        for (auto k = fine.prolong_offsets[i]; k < fine.prolong_offsets[i + 1]; ++k) {
            const auto& term = fine.prolong[k]; fine.restrict[at[term.node]++] = {i, term.weight};
        }
}
MultigridLevel coarsen(MultigridLevel& fine, const MacGrid& grid) {
    const auto n = fine.coordinates.size(); std::vector<std::size_t> roots(n), index(n, none);
    std::iota(roots.begin(), roots.end(), 0);
    const auto find = [&](std::size_t a) {
        while (roots[a] != a) { roots[a] = roots[roots[a]]; a = roots[a]; } return a;
    };
    for (const auto& edge : fine.edges) {
        const auto& a = fine.coordinates[edge.left]; const auto& b = fine.coordinates[edge.right];
        if (a[0] / 2 != b[0] / 2 || a[1] / 2 != b[1] / 2) continue;
        const auto ra = find(edge.left), rb = find(edge.right);
        roots[std::max(ra, rb)] = std::min(ra, rb);
    }
    MultigridLevel coarse; coarse.stride = 2 * fine.stride; fine.parent.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto root = find(i);
        if (index[root] == none) {
            index[root] = coarse.coordinates.size(); const auto& c = fine.coordinates[root]; coarse.coordinates.push_back({c[0] / 2, c[1] / 2});
        }
        fine.parent[i] = index[root];
    }
    struct Candidate { std::size_t left, right, axis, edge; };
    std::vector<Candidate> candidates; candidates.reserve(fine.edges.size());
    for (std::size_t e = 0; e < fine.edges.size(); ++e) {
        const auto& edge = fine.edges[e]; const auto a = fine.parent[edge.left], b = fine.parent[edge.right];
        if (a != b) candidates.push_back({std::min(a, b), std::max(a, b), edge.axis, e});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return std::tie(a.left, a.right, a.axis, a.edge) < std::tie(b.left, b.right, b.axis, b.edge);
    });
    fine.edge_parent.assign(fine.edges.size(), none); fine.edge_scale.assign(fine.edges.size(), 0);
    for (const auto& candidate : candidates) {
        if (coarse.edges.empty() || coarse.edges.back().left != candidate.left || coarse.edges.back().right != candidate.right ||
            coarse.edges.back().axis != candidate.axis) coarse.edges.push_back({candidate.left, candidate.right, candidate.axis, none});
        const auto& edge = fine.edges[candidate.edge]; const auto axis = edge.axis, extent = axis == 0 ? grid.width : grid.height;
        const auto old_span = span(fine.coordinates[edge.left][axis], fine.stride, extent) + span(fine.coordinates[edge.right][axis], fine.stride, extent);
        const auto new_span = span(coarse.coordinates[candidate.left][axis], coarse.stride, extent) + span(coarse.coordinates[candidate.right][axis], coarse.stride, extent);
        fine.edge_parent[candidate.edge] = coarse.edges.size() - 1;
        fine.edge_scale[candidate.edge] = static_cast<double>(old_span) / static_cast<double>(new_span);
    }
    adjacency(coarse); transfers(fine, coarse, grid); return coarse;
}
}
PressureMultigridTopology::PressureMultigridTopology(const MacGrid& grid) : grid_(grid) {
    require(grid.width > 0 && grid.height > 0 && grid.width <= 1000000 / grid.height && grid.walls.size() == grid.width * grid.height &&
        (!grid.periodic_x || grid.width > 1) && (!grid.periodic_y || grid.height > 1), "Invalid multigrid grid");
    for (auto wall : grid.walls) require(wall <= 1, "Invalid multigrid wall");
    levels_.reserve(24); MultigridLevel fine; std::vector<std::size_t> local(grid.walls.size(), none);
    for (std::size_t i = 0; i < grid.walls.size(); ++i) if (!grid.walls[i]) {
        local[i] = active_.size(); active_.push_back(i); fine.coordinates.push_back({i % grid.width, i / grid.width});
    }
    const auto add = [&](std::size_t l, std::size_t r, std::size_t axis, std::size_t face) {
        if (l != none && r != none && local[l] != none && local[r] != none) fine.edges.push_back({local[l], local[r], axis, face});
    };
    for (std::size_t y = 0; y < grid.height; ++y) for (std::size_t x = 0; x < grid.x_width(); ++x)
        add(x > 0 || grid.periodic_x ? (x + grid.width - 1) % grid.width + y * grid.width : none,
            x < grid.width ? x + y * grid.width : none, 0, grid.x_face(x, y));
    for (std::size_t y = 0; y < grid.y_height(); ++y) for (std::size_t x = 0; x < grid.width; ++x)
        add(y > 0 || grid.periodic_y ? x + ((y + grid.height - 1) % grid.height) * grid.width : none,
            y < grid.height ? x + y * grid.width : none, 1, grid.y_face(x, y));
    adjacency(fine); levels_.push_back(std::move(fine));
    while (levels_.back().coordinates.size() > dense_limit && !levels_.back().edges.empty()) {
        require(levels_.size() < 24, "Multigrid hierarchy failed to coarsen");
        auto next = coarsen(levels_.back(), grid); levels_.push_back(std::move(next));
    }
}
PressureMultigrid::PressureMultigrid(std::shared_ptr<const PressureMultigridTopology> topology) : topology_(std::move(topology)) {
    require(topology_ != nullptr, "Missing multigrid topology"); values_.resize(topology_->levels().size());
    for (std::size_t l = 0; l < values_.size(); ++l) {
        auto& value = values_[l]; const auto& level = topology_->levels()[l];
        for (auto* field : {&value.compliance, &value.diagonal, &value.relaxation, &value.rhs, &value.correction, &value.scratch}) field->resize(level.coordinates.size());
        value.conductance.resize(level.edges.size());
    }
    const auto n = topology_->levels().back().coordinates.size();
    if (!topology_->levels().back().edges.empty()) cholesky_.resize(n * n);
}
bool PressureMultigrid::prepare(std::span<const double> compliance, std::span<const double> gx, std::span<const double> gy) {
    prepared_ = false; cycles_ = 0; const auto& grid = topology_->grid();
    require(compliance.size() == grid.walls.size() && gx.size() == grid.x_width() * grid.height && gy.size() == grid.width * grid.y_height(), "Multigrid coefficient shape differs");
    const auto& levels = topology_->levels(); const auto& active = topology_->active_cells();
    for (std::size_t i = 0; i < active.size(); ++i) {
        const double h = compliance[active[i]]; if (!std::isfinite(h) || h <= 0) return false; values_[0].compliance[i] = h;
    }
    for (std::size_t e = 0; e < levels[0].edges.size(); ++e) {
        const auto& edge = levels[0].edges[e]; const double g = (edge.axis == 0 ? gx : gy)[edge.source_face];
        if (!std::isfinite(g) || g < 0) return false;
        values_[0].conductance[e] = g;
    }
    for (std::size_t l = 0; l < levels.size(); ++l) {
        auto& value = values_[l]; const auto& level = levels[l]; value.diagonal = value.compliance;
        for (std::size_t e = 0; e < level.edges.size(); ++e) {
            const auto& edge = level.edges[e]; value.diagonal[edge.left] += value.conductance[e]; value.diagonal[edge.right] += value.conductance[e];
        }
        for (std::size_t i = 0; i < value.diagonal.size(); ++i) {
            if (!std::isfinite(value.diagonal[i]) || value.diagonal[i] <= 0) return false;
            value.relaxation[i] = 1 / value.diagonal[i];
        }
        if (l + 1 == levels.size()) break;
        auto& next = values_[l + 1]; std::fill(next.compliance.begin(), next.compliance.end(), 0); std::fill(next.conductance.begin(), next.conductance.end(), 0);
        for (std::size_t i = 0; i < value.compliance.size(); ++i) next.compliance[level.parent[i]] += value.compliance[i];
        for (std::size_t e = 0; e < level.edges.size(); ++e) if (level.edge_parent[e] != none)
            next.conductance[level.edge_parent[e]] += value.conductance[e] * level.edge_scale[e];
    }
    const auto& last = levels.back(); const auto& value = values_.back(); const auto n = last.coordinates.size();
    if (!last.edges.empty()) {
        std::fill(cholesky_.begin(), cholesky_.end(), 0);
        for (std::size_t i = 0; i < n; ++i) cholesky_[i * n + i] = value.diagonal[i];
        for (std::size_t e = 0; e < last.edges.size(); ++e) {
            const auto& edge = last.edges[e]; cholesky_[edge.left * n + edge.right] -= value.conductance[e]; cholesky_[edge.right * n + edge.left] -= value.conductance[e];
        }
        for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j <= i; ++j) {
            double sum = cholesky_[i * n + j];
            for (std::size_t k = 0; k < j; ++k) sum -= cholesky_[i * n + k] * cholesky_[j * n + k];
            if (i == j) {
                if (!std::isfinite(sum) || sum <= 64 * std::numeric_limits<double>::epsilon() * value.diagonal[i]) return false;
                cholesky_[i * n + j] = std::sqrt(sum);
            } else cholesky_[i * n + j] = sum / cholesky_[j * n + j];
        }
    }
    prepared_ = true; return true;
}
void PressureMultigrid::residual(std::size_t l) {
    auto& value = values_[l]; const auto& level = topology_->levels()[l];
    for (std::size_t i = 0; i < value.rhs.size(); ++i) {
        double sum = value.rhs[i] - value.compliance[i] * value.correction[i];
        for (auto k = level.adjacent_offsets[i]; k < level.adjacent_offsets[i + 1]; ++k) {
            const auto& neighbor = level.adjacent[k]; sum -= value.conductance[neighbor.edge] * (value.correction[i] - value.correction[neighbor.node]);
        }
        value.scratch[i] = sum;
    }
}
void PressureMultigrid::smooth(std::size_t l, bool reverse) {
    // p(t)=(1-t/2)(1-3t/2) has |p(t)|<1 for 0<t<=2, the
    // diagonal-scaled graph operator's spectral interval. Reverse the two
    // stages on the upward sweep to retain a symmetric V-cycle.
    constexpr std::array<double, 2> weights{.5, 1.5};
    for (int pass = 0; pass < 2; ++pass) {
        residual(l); auto& value = values_[l];
        const auto omega = weights[static_cast<std::size_t>(reverse ? 1 - pass : pass)];
        for (std::size_t i = 0; i < value.correction.size(); ++i) value.correction[i] += (omega * value.relaxation[i]) * value.scratch[i];
    }
}
void PressureMultigrid::cycle(std::size_t l) {
    auto& value = values_[l]; const auto& level = topology_->levels()[l];
    if (l + 1 == values_.size()) {
        const auto n = value.rhs.size();
        if (level.edges.empty()) for (std::size_t i = 0; i < n; ++i) value.correction[i] = value.rhs[i] / value.compliance[i];
        else {
            for (std::size_t i = 0; i < n; ++i) {
                double sum = value.rhs[i]; for (std::size_t j = 0; j < i; ++j) sum -= cholesky_[i * n + j] * value.correction[j];
                value.correction[i] = sum / cholesky_[i * n + i];
            }
            for (std::size_t i = n; i-- > 0;) {
                double sum = value.correction[i]; for (std::size_t j = i + 1; j < n; ++j) sum -= cholesky_[j * n + i] * value.correction[j];
                value.correction[i] = sum / cholesky_[i * n + i];
            }
        }
        return;
    }
    smooth(l, false); residual(l); auto& next = values_[l + 1];
    for (std::size_t i = 0; i < next.rhs.size(); ++i) {
        double sum = 0;
        for (auto k = level.restrict_offsets[i]; k < level.restrict_offsets[i + 1]; ++k) {
            const auto& term = level.restrict[k]; sum += term.weight * value.scratch[term.node];
        }
        next.rhs[i] = sum; next.correction[i] = 0;
    }
    cycle(l + 1);
    for (std::size_t i = 0; i < value.correction.size(); ++i) {
        double sum = 0;
        for (auto k = level.prolong_offsets[i]; k < level.prolong_offsets[i + 1]; ++k) {
            const auto& term = level.prolong[k]; sum += term.weight * next.correction[term.node];
        }
        value.correction[i] += sum;
    }
    smooth(l, true);
}
bool PressureMultigrid::apply(std::span<const double> rhs, std::span<double> output) {
    require(rhs.size() == topology_->grid().walls.size() && output.size() == rhs.size(), "Multigrid vector shape differs");
    if (!prepared_) return false;
    auto& first = values_[0]; const auto& active = topology_->active_cells();
    for (std::size_t i = 0; i < active.size(); ++i) {
        const double r = rhs[active[i]]; if (!std::isfinite(r)) return false; first.rhs[i] = r; first.correction[i] = 0;
    }
    cycle(0); ++cycles_;
    for (const auto value : first.correction) if (!std::isfinite(value)) return false;
    std::fill(output.begin(), output.end(), 0);
    for (std::size_t i = 0; i < active.size(); ++i) output[active[i]] = first.correction[i];
    return true;
}
}
