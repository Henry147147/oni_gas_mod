#include "mixed_atmospheres/mac_reconstruction.hpp"
#include "mixed_atmospheres/mac_parcel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>

namespace ma::experimental {
namespace {
constexpr auto none = std::numeric_limits<std::size_t>::max();
constexpr double eps = std::numeric_limits<double>::epsilon();
void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }
struct Sum {
    double hi = 0, lo = 0;
    void add(double value) {
        require_finite(value, "MAC reconstruction operand");
        const double s = hi + value, z = s - hi, e = (hi - (s - z)) + (value - z);
        const double t = lo + e, h = s + t, q = h - s;
        lo = (s - (h - q)) + (t - q); hi = h;
        require_finite(hi, "MAC reconstruction sum"); require_finite(lo, "MAC reconstruction tail");
    }
    [[nodiscard]] double value() const { return hi + lo; }
};
std::optional<double> scaled(double value, double part, double total) {
    if (value == 0 || part == 0) return 0;
    if (part == total) return value;
    int ev = 0, ep = 0, et = 0;
    const double v = std::frexp(value, &ev), p = std::frexp(part, &ep), t = std::frexp(total, &et);
    const double answer = std::ldexp(v * p / t, ev + ep - et);
    if (!std::isfinite(answer) || answer == 0 || std::signbit(answer) != std::signbit(value)) return std::nullopt;
    return answer;
}
struct Edge { std::size_t axis, face, donor, target; MacScaled volume; double cx = 0, cy = 0; };
struct Field { double centre = 0, sx = 0, sy = 0, lower = 0, upper = 0; bool positive = true; };
double evaluated(const Field& field, double theta, double x, double y) {
    if (theta == 0) return field.centre;
    Sum value; value.add(field.centre); value.add(theta * x * field.sx); value.add(theta * y * field.sy); return value.value();
}
}
MacParcelPlan reconstruct_mac_parcels(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, std::span<const double> volumes, const MacMassFluxes& q, double maximum_outgoing) {
    require_mac_energy_environment();
    model.validate_registry(registry); static_cast<void>(mac_face_inertias(registry, source, model, volumes));
    const auto& g = source.grid; const auto count = source.cells.size(), species = registry.size();
    const auto qx = mac_field_values(q.x, q.full_x), qy = mac_field_values(q.y, q.full_y);
    static_cast<void>(mac_field_values(source.momentum_x, source.full_momentum_x));
    static_cast<void>(mac_field_values(source.momentum_y, source.full_momentum_y));
    require(volumes.size() == count && q.x.size() == g.x_width() * g.height && q.y.size() == g.width * g.y_height() &&
        std::isfinite(maximum_outgoing) && maximum_outgoing > 0 && maximum_outgoing <= .45, "Invalid MAC reconstruction volume/CFL shapes");
    MacParcelPlan result; result.source_volumes_m3.assign(volumes.begin(), volumes.end()); result.retained_volumes_m3.resize(count); result.limiter.resize(count);
    result.retained.reserve(count); std::vector<double> positive_energy(count); std::vector<MacScaledSum> outgoing(count);
    std::vector<std::array<std::size_t, 4>> by_donor(count); for (auto& row : by_donor) row.fill(none);
    std::vector<bool> flat_coordinate(count, false);
    std::vector<Edge> edges; edges.reserve(q.x.size() + q.y.size()); std::size_t sparse_count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        require(std::isfinite(volumes[i]) && (g.walls[i] ? volumes[i] == 0 : volumes[i] > 0), "Invalid reconstruction material volume");
        sparse_count += source.cells[i].passive_quantities.size(); require(sparse_count <= 16000000, "MAC reconstruction passive capacity exceeded");
        if (!g.walls[i]) {
            const auto eos = model.evaluate(source.cells[i].mixture, volumes[i]);
            if (eos.vacuum_volume_m3 > 32 * eps * volumes[i]) throw std::domain_error("MAC reconstruction source contains internal void");
            positive_energy[i] = eos.thermal_energy_j + eos.elastic_energy_j; require_finite(positive_energy[i], "MAC positive reconstruction energy");
        }
        result.retained.push_back({MixtureInventory(species), {}});
    }
    const auto neighbor = [&](std::size_t cell, int axis, int direction) {
        const auto x = cell % g.width, y = cell / g.width;
        if ((axis == 0 && ((direction < 0 && x == 0) || (direction > 0 && x + 1 == g.width)) && !g.periodic_x) ||
            (axis == 1 && ((direction < 0 && y == 0) || (direction > 0 && y + 1 == g.height)) && !g.periodic_y)) return none;
        const auto nx = axis == 0 ? (direction < 0 ? (x + g.width - 1) % g.width : (x + 1) % g.width) : x;
        const auto ny = axis == 1 ? (direction < 0 ? (y + g.height - 1) % g.height : (y + 1) % g.height) : y;
        const auto index = nx + ny * g.width; return g.walls[index] ? none : index;
    };
    const auto exceeds = [](MacScaled a, MacScaled b) { MacScaledSum difference; difference.add(a); difference.add(b.negated()); return difference.sign() > 0; };
    const auto midpoint = [](MacScaled a, MacScaled b) { MacScaledSum sum; sum.add(a); sum.add(b); return sum.value().shifted(-1); };
    const auto add = [&](std::size_t axis, std::size_t face, std::size_t left, std::size_t right, MacScaled sweep) {
        if (left == none || right == none || g.walls[left] || g.walls[right]) { require(sweep.is_zero(), "Reconstruction sweep crosses wall/exterior"); return; }
        if (sweep.is_zero()) return;
        const bool positive = sweep.mantissa() > 0; const auto donor = positive ? left : right, target = positive ? right : left;
        auto& list = by_donor[donor]; auto slot = std::find(list.begin(), list.end(), none);
        require(slot != list.end(), "Too many MAC reconstruction outflows"); *slot = edges.size();
        const auto volume = sweep.magnitude();
        if (exceeds(volume, MacScaled::from_double(volumes[donor]).multiplied(maximum_outgoing))) throw std::domain_error("MAC reconstructed face volume CFL exceeded");
        const double normal = .5 * (positive ? 1 : -1) * (1 - volume.shared(1, volumes[donor]).project().value);
        const auto x = donor % g.width, y = donor / g.width;
        double transverse = (axis == 0 ? midpoint(qy[g.y_face(x, y)], qy[g.y_face(x, (y + 1) % g.y_height())]) :
            midpoint(qx[g.x_face(x, y)], qx[g.x_face((x + 1) % g.x_width(), y)])).shared(1, volumes[donor]).multiplied(-.5).project().value;
        require_finite(normal, "MAC normal reconstruction coordinate");
        if (!std::isfinite(transverse)) { flat_coordinate[donor] = true; transverse = 0; }
        edges.push_back({axis, face, donor, target, volume, axis == 0 ? normal : transverse, axis == 0 ? transverse : normal}); outgoing[donor].add(volume);
    };
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x)
        add(0, g.x_face(x, y), x > 0 || g.periodic_x ? (x + g.width - 1) % g.width + y * g.width : none,
            x < g.width ? x + y * g.width : none, qx[g.x_face(x, y)]);
    for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x)
        add(1, g.y_face(x, y), y > 0 || g.periodic_y ? x + ((y + g.height - 1) % g.height) * g.width : none,
            y < g.height ? x + y * g.width : none, qy[g.y_face(x, y)]);
    require(edges.size() <= 16000000 / species, "MAC reconstructed parcel capacity exceeded");
    std::vector<std::optional<MacPrimalParcel>> prepared(edges.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (g.walls[i]) continue;
        if (exceeds(outgoing[i].value(), MacScaled::from_double(volumes[i]).multiplied(maximum_outgoing))) throw std::domain_error("MAC reconstructed volume CFL exceeded");
        MacScaledSum retained_amount; retained_amount.add(volumes[i]);
        for (std::size_t k = 0; k < outgoing[i].size(); ++k) retained_amount.add(outgoing[i].component(k).negated());
        const double retained_volume = store_mac_primary(retained_amount, true); result.retained_volumes_m3[i] = retained_volume;
        const std::array<std::size_t, 4> adjacent{neighbor(i, 0, -1), neighbor(i, 0, 1), neighbor(i, 1, -1), neighbor(i, 1, 1)};
        std::set<std::uint64_t> identities;
        for (const auto cell : {i, adjacent[0], adjacent[1], adjacent[2], adjacent[3]}) if (cell != none)
            for (const auto& entry : source.cells[cell].passive_quantities) identities.insert(entry.identity);
        require(identities.size() <= 327685, "MAC reconstruction stencil passive capacity exceeded");
        const std::vector<std::uint64_t> ids(identities.begin(), identities.end());
        std::vector<Field> fields(species + 2 + ids.size()); bool flat = flat_coordinate[i];
        const auto quantity = [&](std::size_t cell, std::size_t field) {
            if (field < species) return source.cells[cell].mixture.masses[field];
            if (field == species) return source.cells[cell].mixture.energy;
            if (field == species + 1) return positive_energy[cell];
            const auto id = ids[field - species - 2]; const auto& values = source.cells[cell].passive_quantities;
            const auto found = std::lower_bound(values.begin(), values.end(), id, [](const PassiveQuantity& p, std::uint64_t value) { return p.identity < value; });
            return found != values.end() && found->identity == id ? found->quantity : 0.;
        };
        for (std::size_t field = 0; field < fields.size(); ++field) {
            auto& f = fields[field]; f.centre = quantity(i, field); f.lower = f.upper = f.centre; f.positive = field != species;
            std::array<double, 4> values{f.centre, f.centre, f.centre, f.centre};
            for (std::size_t n = 0; n < adjacent.size(); ++n) if (adjacent[n] != none) {
                const auto value = scaled(quantity(adjacent[n], field), volumes[i], volumes[adjacent[n]]);
                if (!value) { flat = true; continue; } values[n] = *value; f.lower = std::min(f.lower, *value); f.upper = std::max(f.upper, *value);
            }
            if (adjacent[0] != none && adjacent[1] != none) f.sx = .5 * (values[1] - values[0]);
            if (adjacent[2] != none && adjacent[3] != none) f.sy = .5 * (values[3] - values[2]);
            if (!std::isfinite(f.sx) || !std::isfinite(f.sy) || (values[1] != values[0] && adjacent[0] != none && adjacent[1] != none && f.sx == 0) ||
                (values[3] != values[2] && adjacent[2] != none && adjacent[3] != none && f.sy == 0)) flat = true;
        }
        std::vector<std::pair<double, double>> points{{-.5, -.5}, {-.5, .5}, {.5, -.5}, {.5, .5}};
        Sum rx, ry;
        for (auto edge : by_donor[i]) if (edge != none) {
            const double ratio = edges[edge].volume.shared(1, retained_volume).project().value;
            points.emplace_back(edges[edge].cx, edges[edge].cy); rx.add(-ratio * edges[edge].cx); ry.add(-ratio * edges[edge].cy);
        }
        points.emplace_back(rx.value(), ry.value()); double theta = flat ? 0 : 1;
        if (!flat) for (const auto& field : fields) {
            for (const auto& [x, y] : points) {
                if (!std::isfinite(x * field.sx) || !std::isfinite(y * field.sy)) { flat = true; theta = 0; break; }
                Sum increment; increment.add(x * field.sx); increment.add(y * field.sy); const double delta = increment.value();
                if (delta > 0) theta = std::min(theta, (field.upper - field.centre) / delta);
                if (delta < 0) {
                    theta = std::min(theta, (field.lower - field.centre) / delta);
                    if (field.positive && field.centre > 0) theta = std::min(theta, (-.5 * field.centre) / delta);
                }
            }
            if (flat) break;
        }
        if (flat) ++result.unresolved_stencil_cells;
        require(std::isfinite(theta) && theta >= 0 && theta <= 1, "Invalid reconstructed MAC limiter calculation");
        const auto values = [&](double coefficient, double x, double y) {
            MacMaterialCell cell{MixtureInventory(species), {}};
            const auto value = [&](std::size_t index) {
                return evaluated(fields[index], coefficient, x, y);
            };
            for (std::size_t s = 0; s < species; ++s) cell.mixture.masses[s] = value(s);
            cell.mixture.energy = value(species);
            for (std::size_t p = 0; p < ids.size(); ++p) { const auto v = value(species + 2 + p); if (v != 0) cell.passive_quantities.push_back({ids[p], v}); }
            return cell;
        };
        const auto admitted = [&](const MacMaterialCell& cell, double volume) {
            model.validate_inventory(cell.mixture); validate_passive_quantities(cell.passive_quantities);
            const auto eos = model.evaluate(cell.mixture, volume);
            if (eos.vacuum_volume_m3 > 32 * eps * volume) throw std::domain_error("Reconstructed MAC state has internal void");
        };
        bool accepted = false;
        for (std::size_t attempt = 0; attempt <= 33; ++attempt) {
            std::vector<MacScaledSum> retained(species + 1 + ids.size());
            for (std::size_t f = 0; f < species + 1; ++f) retained[f].add(quantity(i, f));
            for (std::size_t p = 0; p < ids.size(); ++p) retained[species + 1 + p].add(quantity(i, species + 2 + p));
            try {
                for (auto edge : by_donor[i]) if (edge != none) {
                    const auto& e = edges[edge]; auto prototype = values(theta, e.cx, e.cy);
                    admitted(prototype, volumes[i]);
                    const auto volume_projection = e.volume.project();
                    MacPrimalParcel parcel{e.axis, e.face, i, e.target, volume_projection.value, std::move(prototype), volumes[i], volume_projection.rounded ? e.volume : MacScaled{}};
                    auto projected = parcel.contents; bool rounded = false;
                    const auto projection = [&](double value) {
                        const auto p = mac_parcel_quantity(parcel, value).project(); rounded = rounded || p.rounded; return p.value;
                    };
                    for (auto& mass : projected.mixture.masses) mass = projection(mass);
                    projected.mixture.energy = projection(projected.mixture.energy);
                    for (auto& p : projected.passive_quantities) p.quantity = projection(p.quantity);
                    // Preserve the original actual-amount representation whenever
                    // every field projects exactly. Otherwise retain the complete
                    // common-scale prototype, including every positive trace.
                    if (!rounded && parcel.full_volume_m3.is_zero()) { parcel.contents = std::move(projected); parcel.reference_volume_m3 = 0; }
                    admitted(mac_parcel_contents_at(parcel, volumes[i]), volumes[i]);
                    for (std::size_t s = 0; s < species; ++s) retained[s].add(mac_parcel_quantity(parcel, parcel.contents.mixture.masses[s]).negated());
                    retained[species].add(mac_parcel_quantity(parcel, parcel.contents.mixture.energy).negated());
                    for (const auto& p : parcel.contents.passive_quantities) {
                        const auto index = static_cast<std::size_t>(std::lower_bound(ids.begin(), ids.end(), p.identity) - ids.begin());
                        retained[species + 1 + index].add(mac_parcel_quantity(parcel, p.quantity).negated());
                    }
                    prepared[edge] = std::move(parcel);
                }
                auto& cell = result.retained[i];
                for (std::size_t s = 0; s < species; ++s) cell.mixture.masses[s] = store_mac_primary(retained[s], true);
                cell.mixture.energy = store_mac_primary(retained[species], false);
                cell.passive_quantities.clear();
                for (std::size_t p = 0; p < ids.size(); ++p) { const double v = store_mac_primary(retained[species + 1 + p], true); if (v != 0) cell.passive_quantities.push_back({ids[p], v}); }
                admitted(cell, retained_volume); accepted = true;
            } catch (const ReferenceError& error) {
                if (theta == 0 || error.code() == ReferenceErrorCode::invalid_model || error.code() == ReferenceErrorCode::incompatible_entropy_reference) throw;
            } catch (const std::domain_error&) { if (theta == 0) throw; }
            catch (const std::invalid_argument&) { if (theta == 0) throw; }
            if (accepted) break;
            ++result.eos_backtracks; theta = attempt < 32 ? theta * .5 : 0;
        }
        require(accepted, "MAC reconstruction EOS backtracking exhausted"); result.limiter[i] = theta;
    }
    result.parcels.reserve(edges.size());
    for (auto& parcel : prepared) { require(parcel.has_value(), "Missing reconstructed MAC parcel"); result.parcels.push_back(std::move(*parcel)); }
    return result;
}
}
