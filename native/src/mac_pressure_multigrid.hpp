#pragma once
#include "mixed_atmospheres/mac_transport.hpp"
#include <memory>

namespace ma::experimental::interval_detail {
struct MultigridEdge { std::size_t left, right, axis, source_face; };
struct MultigridNeighbor { std::size_t node, edge; };
struct MultigridTerm { std::size_t node; double weight; };
struct MultigridLevel {
    std::size_t stride = 1;
    std::vector<std::array<std::size_t, 2>> coordinates;
    std::vector<MultigridEdge> edges;
    std::vector<std::size_t> adjacent_offsets;
    std::vector<MultigridNeighbor> adjacent;
    // Piecewise-constant aggregation builds a positive rediscretized coarse
    // operator. Linear interpolation and its actual transpose transfer vectors.
    std::vector<std::size_t> parent, edge_parent;
    std::vector<double> edge_scale;
    std::vector<std::size_t> prolong_offsets, restrict_offsets;
    std::vector<MultigridTerm> prolong, restrict;
};
class PressureMultigridTopology {
public:
    explicit PressureMultigridTopology(const MacGrid&);
    [[nodiscard]] const MacGrid& grid() const noexcept { return grid_; }
    [[nodiscard]] const std::vector<std::size_t>& active_cells() const noexcept { return active_; }
    [[nodiscard]] const std::vector<MultigridLevel>& levels() const noexcept { return levels_; }
private:
    MacGrid grid_;
    std::vector<std::size_t> active_;
    std::vector<MultigridLevel> levels_;
};
struct MultigridValues {
    std::vector<double> compliance, conductance, diagonal, relaxation;
    std::vector<double> rhs, correction, scratch;
};
// One fixed, symmetric V-cycle per application. All numerical coefficients are
// replaced before a Newton solve; topology is immutable and may be shared.
class PressureMultigrid {
public:
    explicit PressureMultigrid(std::shared_ptr<const PressureMultigridTopology>);
    [[nodiscard]] bool prepare(std::span<const double> compliance,
        std::span<const double> conductance_x, std::span<const double> conductance_y);
    [[nodiscard]] bool apply(std::span<const double> rhs, std::span<double> result);
    [[nodiscard]] const std::vector<MultigridValues>& values() const noexcept { return values_; }
    [[nodiscard]] std::size_t cycles() const noexcept { return cycles_; }
private:
    std::shared_ptr<const PressureMultigridTopology> topology_;
    std::vector<MultigridValues> values_;
    std::vector<double> cholesky_;
    bool prepared_ = false;
    std::size_t cycles_ = 0;
    void residual(std::size_t);
    void smooth(std::size_t, bool reverse);
    void cycle(std::size_t);
};
}
