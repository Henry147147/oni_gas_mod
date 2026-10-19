// The two real pressure moments reduce to one complex symmetric system.
// COCG uses transpose products, not Hermitian products. An undecidable
// bilinear pivot requests the retained real Schur solver; it never repairs
// pressure, inventory or energy. MAC_PRESSURE_MOMENTS.md owns the equations.
#include "mac_pressure_moment_multigrid.hpp"
#include "mac_kernel_detail.hpp"

namespace ma::experimental::mac_kernel {
namespace {
constexpr auto missing = std::numeric_limits<std::size_t>::max();
bool finite(MomentComplex value) { return value.finite(); }
}
PressureMomentMultigrid::PressureMomentMultigrid(std::shared_ptr<const interval_detail::PressureMultigridTopology> topology)
    : topology_(std::move(topology)) {
    require(topology_ != nullptr, "Missing moment multigrid topology");
    values_.resize(topology_->levels().size());
    for (std::size_t l = 0; l < values_.size(); ++l) {
        auto& v = values_[l]; const auto& level = topology_->levels()[l];
        for (auto* field : {&v.mass, &v.inverse, &v.rhs, &v.correction, &v.scratch}) field->resize(level.coordinates.size());
        v.conductance.resize(level.edges.size());
    }
    const auto n = topology_->active_cells().size(); compliance_.resize(n);
    for (auto* v : {&x_, &r_, &z_, &p_, &Ap_}) v->resize(n);
    parts_.resize((n + 255) / 256);
    const auto& last = topology_->levels().back();
    // Disconnected singletons can leave arbitrarily many coarse nodes. Their
    // independent diagonal solve must never allocate a quadratic matrix.
    if (!last.edges.empty()) lu_.resize(last.coordinates.size() * last.coordinates.size());
}
std::size_t PressureMomentMultigrid::storage_bytes() const noexcept {
    std::size_t total = topology_->storage_bytes() + values_.capacity() * sizeof(Values) + parts_.capacity() * sizeof(DotPart);
    const auto add = [&](const auto& v) { total += v.capacity() * sizeof(typename std::decay_t<decltype(v)>::value_type); };
    add(compliance_); add(lu_); add(x_); add(r_); add(z_); add(p_); add(Ap_);
    for (const auto& v : values_) { add(v.mass); add(v.inverse); add(v.rhs); add(v.correction); add(v.scratch); add(v.conductance); }
    return total;
}
bool PressureMomentMultigrid::prepare(std::span<const double> compliance, std::span<const double> diagonal,
    std::span<const double> wx, std::span<const double> wy) {
    prepared_ = false;
    const auto& grid = topology_->grid(); const auto& levels = topology_->levels(); const auto& active = topology_->active_cells();
    require(compliance.size() == grid.walls.size() && diagonal.size() == compliance.size() &&
        wx.size() == grid.x_width() * grid.height && wy.size() == grid.width * grid.y_height(), "Moment multigrid coefficient shape differs");
    auto& first = values_[0];
    for (std::size_t i = 0; i < active.size(); ++i) {
        const auto cell = active[i]; const double c = compliance[cell], boundary = diagonal[cell] - c;
        if (!(c > 0) || !std::isfinite(c) || !(boundary >= 0) || !std::isfinite(boundary)) return false;
        compliance_[i] = c; first.mass[i] = MomentComplex{c, 0} + boundary * coupling_;
    }
    for (std::size_t e = 0; e < levels[0].edges.size(); ++e) {
        const auto& edge = levels[0].edges[e]; const double value = (edge.axis == 0 ? wx : wy)[edge.source_face];
        if (!(value >= 0) || !std::isfinite(value)) return false;
        first.conductance[e] = value;
    }
    for (std::size_t l = 0; l < levels.size(); ++l) {
        const auto& level = levels[l]; auto& v = values_[l];
        for (std::size_t i = 0; i < v.mass.size(); ++i) {
            double links = 0;
            for (auto k = level.adjacent_offsets[i]; k < level.adjacent_offsets[i + 1]; ++k) links += v.conductance[level.adjacent[k].edge];
            const auto d = v.mass[i] + links * coupling_;
            v.inverse[i] = MomentComplex{1, 0} / d;
            if (!(v.mass[i].re > 0) || !finite(v.mass[i]) || !finite(v.inverse[i]) || v.inverse[i].magnitude() == 0) return false;
        }
        if (l + 1 == levels.size()) break;
        auto& next = values_[l + 1]; std::fill(next.mass.begin(), next.mass.end(), MomentComplex{});
        std::fill(next.conductance.begin(), next.conductance.end(), 0);
        for (std::size_t i = 0; i < v.mass.size(); ++i) next.mass[level.parent[i]] += v.mass[i];
        for (std::size_t e = 0; e < v.conductance.size(); ++e) if (level.edge_parent[e] != missing)
            next.conductance[level.edge_parent[e]] += v.conductance[e] * level.edge_scale[e];
    }
    const auto& last = levels.back(); const auto& v = values_.back(); const auto n = last.coordinates.size();
    if (!last.edges.empty()) {
        std::fill(lu_.begin(), lu_.end(), MomentComplex{});
        for (std::size_t i = 0; i < n; ++i) lu_[i * n + i] = v.mass[i];
        for (std::size_t e = 0; e < last.edges.size(); ++e) {
            const auto& edge = last.edges[e]; const auto value = v.conductance[e] * coupling_;
            lu_[edge.left * n + edge.left] += value; lu_[edge.right * n + edge.right] += value;
            lu_[edge.left * n + edge.right] -= value; lu_[edge.right * n + edge.left] -= value;
        }
        std::vector<double> original(n);
        for (std::size_t i = 0; i < n; ++i) original[i] = lu_[i * n + i].magnitude();
        for (std::size_t k = 0; k < n; ++k) {
            const auto pivot = lu_[k * n + k];
            if (!finite(pivot) || !(pivot.re > 0) || !(pivot.magnitude() > 64 * eps * original[k])) return false;
            for (std::size_t i = k + 1; i < n; ++i) {
                lu_[i * n + k] = lu_[i * n + k] / pivot;
                for (std::size_t j = k + 1; j < n; ++j) lu_[i * n + j] -= lu_[i * n + k] * lu_[k * n + j];
            }
        }
        for (const auto value : lu_) if (!finite(value)) return false;
    }
    prepared_ = true; return true;
}
MomentComplex PressureMomentMultigrid::residual(std::size_t l, std::size_t i) const {
    const auto& v = values_[l]; const auto& level = topology_->levels()[l]; MomentComplex gradient;
    for (auto k = level.adjacent_offsets[i]; k < level.adjacent_offsets[i + 1]; ++k) {
        const auto& neighbor = level.adjacent[k]; gradient += v.conductance[neighbor.edge] * (v.correction[i] - v.correction[neighbor.node]);
    }
    return (v.rhs[i] - v.mass[i] * v.correction[i]) - coupling_ * gradient;
}
void PressureMomentMultigrid::smooth(std::size_t l, bool upward) {
    auto& v = values_[l]; constexpr std::array<double, 2> weights{.5, 1.5};
    if (!upward) rows(v.rhs.size(), [&](std::size_t i) { v.correction[i] = (.5 * v.inverse[i]) * v.rhs[i]; });
    for (int pass = upward ? 0 : 1; pass < 2; ++pass) {
        const double omega = weights[static_cast<std::size_t>(upward ? 1 - pass : pass)];
        rows(v.rhs.size(), [&](std::size_t i) { v.scratch[i] = v.correction[i] + (omega * v.inverse[i]) * residual(l, i); });
        v.correction.swap(v.scratch);
    }
}
void PressureMomentMultigrid::cycle(std::size_t l) {
    auto& v = values_[l]; const auto& level = topology_->levels()[l];
    if (l + 1 == values_.size()) {
        const auto n = v.rhs.size();
        if (level.edges.empty()) rows(n, [&](std::size_t i) { v.correction[i] = v.rhs[i] / v.mass[i]; });
        else {
            for (std::size_t i = 0; i < n; ++i) {
                auto value = v.rhs[i]; for (std::size_t j = 0; j < i; ++j) value -= lu_[i * n + j] * v.correction[j]; v.correction[i] = value;
            }
            for (std::size_t i = n; i-- > 0;) {
                auto value = v.correction[i]; for (std::size_t j = i + 1; j < n; ++j) value -= lu_[i * n + j] * v.correction[j];
                v.correction[i] = value / lu_[i * n + i];
            }
        }
        return;
    }
    smooth(l, false); rows(v.rhs.size(), [&](std::size_t i) { v.scratch[i] = residual(l, i); });
    auto& next = values_[l + 1];
    rows(next.rhs.size(), [&](std::size_t i) {
        MomentComplex value;
        for (auto k = level.restrict_offsets[i]; k < level.restrict_offsets[i + 1]; ++k) {
            const auto& term = level.restrict[k]; value += term.weight * v.scratch[term.node];
        }
        next.rhs[i] = value;
    });
    cycle(l + 1);
    rows(v.rhs.size(), [&](std::size_t i) {
        MomentComplex value;
        for (auto k = level.prolong_offsets[i]; k < level.prolong_offsets[i + 1]; ++k) {
            const auto& term = level.prolong[k]; value += term.weight * next.correction[term.node];
        }
        v.correction[i] += value;
    });
    smooth(l, true);
}
void PressureMomentMultigrid::apply(std::span<const MomentComplex> in, std::span<MomentComplex> out) {
    const auto& v = values_[0]; const auto& level = topology_->levels()[0];
    rows(in.size(), [&](std::size_t i) {
        MomentComplex gradient;
        for (auto k = level.adjacent_offsets[i]; k < level.adjacent_offsets[i + 1]; ++k) {
            const auto& neighbor = level.adjacent[k]; gradient += v.conductance[neighbor.edge] * (in[i] - in[neighbor.node]);
        }
        out[i] = v.mass[i] * in[i] + coupling_ * gradient;
    });
}
void PressureMomentMultigrid::precondition(SolveResult& result) {
    auto& first = values_[0]; rows(r_.size(), [&](std::size_t i) { first.rhs[i] = r_[i]; });
    cycle(0); ++result.cycles;
    rows(r_.size(), [&](std::size_t i) { z_[i] = first.correction[i]; });
}
PressureMomentMultigrid::Dot PressureMomentMultigrid::dot(std::span<const MomentComplex> a,
    std::span<const MomentComplex> b, bool conjugate) {
    blocks(parts_.size(), [&](std::size_t block) {
        DotPart part;
        for (auto i = block * 256; i < std::min(a.size(), block * 256 + 256); ++i) {
            const auto av = a[i], bv = b[i];
            part.sum[0] += av.re * bv.re + (conjugate ? av.im * bv.im : -av.im * bv.im);
            part.sum[1] += av.re * bv.im + (conjugate ? -av.im * bv.re : av.im * bv.re);
            part.sum[2] += (std::abs(av.re) + std::abs(av.im)) * (std::abs(bv.re) + std::abs(bv.im));
            part.maximum = std::max({part.maximum, std::abs(av.re), std::abs(av.im), std::abs(bv.re), std::abs(bv.im)});
        }
        parts_[block] = part;
    });
    std::array<DD, 3> sums; double maximum = 0;
    for (const auto& part : parts_) { for (std::size_t j = 0; j < 3; ++j) sums[j].add(part.sum[j]); maximum = std::max(maximum, part.maximum); }
    return {{sums[0].value(), sums[1].value()}, sums[2].value(), maximum};
}
bool PressureMomentMultigrid::Dot::determined() const {
    return value.finite() && std::isfinite(scale) && value.magnitude() > 512 * eps * scale;
}
double PressureMomentMultigrid::norm(std::span<const MomentComplex> v) {
    const auto d = dot(v, v, true);
    if (!d.value.finite() || !(d.value.re >= 0) || (d.value.re == 0 && d.maximum != 0)) return std::numeric_limits<double>::infinity();
    return std::sqrt(d.value.re);
}
PressureMomentMultigrid::SolveResult PressureMomentMultigrid::solve(std::span<const double> ra, std::span<const double> rb,
    std::span<double> da, std::span<double> db, double rtol, double atol, std::size_t maximum, bool clear_inactive) {
    const auto total = topology_->grid().walls.size(); const auto& active = topology_->active_cells(); const auto n = active.size();
    require(ra.size() == total && rb.size() == total && da.size() == total && db.size() == total && maximum > 0 &&
        std::isfinite(rtol) && rtol >= 0 && std::isfinite(atol) && atol >= 0, "Invalid coupled moment linear request");
    SolveResult result;
    if (!prepared_) { result.flagged = true; return result; }
    const double root3 = std::sqrt(3.); const MomentComplex beta{-root3 / 2, .5};
    const auto rhs = [&](std::size_t i) { return MomentComplex{ra[active[i]], 0} + rb[active[i]] * beta; };
    rows(n, [&](std::size_t i) { x_[i] = {}; r_[i] = rhs(i); });
    result.residual = norm(r_);
    if (!std::isfinite(result.residual)) { result.flagged = true; return result; }
    const double threshold = std::max(atol * std::sqrt(static_cast<double>(n)), rtol * result.residual);
    if (result.residual <= threshold) result.converged = true;
    Dot rho;
    if (!result.converged) { precondition(result); std::copy(z_.begin(), z_.end(), p_.begin()); rho = dot(r_, z_, false); }
    for (std::size_t iteration = 0; !result.converged && iteration < maximum; ++iteration) {
        if (!rho.determined()) { result.flagged = true; break; }
        apply(p_, Ap_); const auto denominator = dot(p_, Ap_, false);
        if (!denominator.determined()) { result.flagged = true; break; }
        const auto alpha = rho.value / denominator.value;
        if (!alpha.finite()) { result.flagged = true; break; }
        rows(n, [&](std::size_t i) { x_[i] += alpha * p_[i]; r_[i] -= alpha * Ap_[i]; });
        ++result.iterations; result.residual = norm(r_);
        if (!std::isfinite(result.residual)) { result.flagged = true; break; }
        if (result.residual <= threshold) {
            // Certify against the actual matrix, not just the recursively
            // updated residual. A residual replacement restarts COCG.
            apply(x_, Ap_); rows(n, [&](std::size_t i) { r_[i] = rhs(i) - Ap_[i]; });
            result.residual = norm(r_);
            if (!std::isfinite(result.residual)) { result.flagged = true; break; }
            if (result.residual <= threshold) { result.converged = true; break; }
            precondition(result); std::copy(z_.begin(), z_.end(), p_.begin()); rho = dot(r_, z_, false); continue;
        }
        precondition(result); const auto next = dot(r_, z_, false); const auto direction = next.value / rho.value;
        if (!next.determined() || !direction.finite()) { result.flagged = true; break; }
        rho = next; rows(n, [&](std::size_t i) { p_[i] = z_[i] + direction * p_[i]; });
    }
    if (!result.converged) return result;
    rows(n, [&](std::size_t i) {
        z_[i] = {compliance_[i] * (x_[i].re + root3 * x_[i].im), (2 * compliance_[i]) * x_[i].im};
    });
    for (const auto value : z_) if (!value.finite()) { result.flagged = true; result.converged = false; return result; }
    if (clear_inactive) rows(total, [&](std::size_t i) { da[i] = 0; db[i] = 0; });
    rows(n, [&](std::size_t i) { da[active[i]] = z_[i].re; db[active[i]] = z_[i].im; });
    return result;
}

bool PressureMomentMultigrid::precondition_pair(std::span<const double> ra, std::span<const double> rb,
    std::span<double> da, std::span<double> db, bool clear_inactive) {
    const auto total = topology_->grid().walls.size(); const auto& active = topology_->active_cells(); const auto n = active.size();
    require(ra.size() == total && rb.size() == total && da.size() == total && db.size() == total,
        "Invalid coupled inverse request");
    if (!prepared_) return false;
    const double root3 = std::sqrt(3.); const MomentComplex beta{-root3 / 2, .5};
    rows(n, [&](std::size_t i) { r_[i] = MomentComplex{ra[active[i]], 0} + rb[active[i]] * beta; });
    SolveResult counter; precondition(counter);
    rows(n, [&](std::size_t i) {
        x_[i] = {compliance_[i] * (z_[i].re + root3 * z_[i].im), (2 * compliance_[i]) * z_[i].im};
    });
    for (const auto value : x_) if (!value.finite()) return false;
    if (clear_inactive) rows(total, [&](std::size_t i) { da[i] = 0; db[i] = 0; });
    rows(n, [&](std::size_t i) { da[active[i]] = x_[i].re; db[active[i]] = x_[i].im; });
    return true;
}

bool PressureMomentMultigrid::apply_pair(std::span<const double> a, std::span<const double> b,
    std::span<double> ra, std::span<double> rb, bool clear_inactive) {
    const auto total = topology_->grid().walls.size(); const auto& active = topology_->active_cells(); const auto n = active.size();
    require(a.size() == total && b.size() == total && ra.size() == total && rb.size() == total, "Invalid coupled operator request");
    if (!prepared_) return false;
    const double root3 = std::sqrt(3.);
    rows(n, [&](std::size_t j) { const auto i = active[j]; x_[j] = {(a[i] - (.5 * root3) * b[i]) / compliance_[j], (.5 * b[i]) / compliance_[j]}; });
    apply(x_, Ap_);
    rows(n, [&](std::size_t j) { z_[j] = {Ap_[j].re + root3 * Ap_[j].im, 2 * Ap_[j].im}; });
    for (const auto value : z_) if (!value.finite()) return false;
    if (clear_inactive) rows(total, [&](std::size_t i) { ra[i] = 0; rb[i] = 0; });
    rows(n, [&](std::size_t j) { ra[active[j]] = z_[j].re; rb[active[j]] = z_[j].im; });
    return true;
}

void solve_pressure_moments_coupled(Context& c, const LinearSystem& sys, const std::vector<double>& compliance,
    const std::vector<double>& ra, const std::vector<double>& rb, std::vector<double>& da, std::vector<double>& db,
    double rtol, double atol, bool first_linearization) {
    auto& m = *c.ws.moments;
    if (first_linearization) {
        MacGrid active = c.source_grid;
        for (std::size_t i = 0; i < sys.active.size(); ++i) active.walls[i] = sys.active[i] ? 0 : 1;
        const auto same = [&](const MacGrid& old) {
            return old.width == active.width && old.height == active.height && old.spacing_m == active.spacing_m &&
                old.depth_m == active.depth_m && old.periodic_x == active.periodic_x && old.periodic_y == active.periodic_y && old.walls == active.walls;
        };
        if (!m.coupled || !same(m.coupled->topology().grid())) {
            m.coupled = std::make_unique<PressureMomentMultigrid>(std::make_shared<const interval_detail::PressureMultigridTopology>(active));
            ++c.counters.topology_builds;
        }
        m.coupled->set_executor([](const void* context, std::size_t blocks, interval_detail::BlockFunction fn, const void* job) {
            if (blocks < 32) { for (std::size_t b = 0; b < blocks; ++b) fn(job, b); }
            else static_cast<Pool*>(const_cast<void*>(context))->run(blocks, [&](std::size_t b) { fn(job, b); });
        }, &c.exec.pool);
        m.coupled_ready = m.coupled->prepare(compliance, sys.diag, sys.wx, sys.wy);
        c.counters.multigrid_levels = std::max(c.counters.multigrid_levels, m.coupled->topology().levels().size());
    }
    if (m.coupled_ready) {
        da.resize(c.g.N); db.resize(c.g.N); ++c.counters.multigrid_solves;
        const auto result = m.coupled->solve(ra, rb, da, db, rtol, atol);
        c.counters.linear += result.iterations; c.counters.multigrid_cycles += result.cycles;
        if (result.converged) return;
    }
    ++c.counters.multigrid_fallbacks;
    solve_pressure_moments_linear(c, sys, compliance, ra, rb, da, db, rtol, atol);
}
}
