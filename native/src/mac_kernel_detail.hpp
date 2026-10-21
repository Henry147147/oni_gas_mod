#pragma once
// Internal structures of the production substep kernel (MAC_KERNEL.md).
// Structure-of-arrays binary64 storage, fixed row blocks and deterministic
// block-ordered reductions. Nothing here is part of the owner API.
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mac_pressure_multigrid.hpp"
#include "mac_pressure_moment_multigrid.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ma::experimental::mac_kernel {
using Index = std::uint32_t;
inline constexpr Index none = std::numeric_limits<Index>::max();
inline constexpr double eps = std::numeric_limits<double>::epsilon();
inline constexpr double unit_roundoff = eps / 2;

// MA_KERNEL_DEBUG prints rejections, Newton progress and audit escalations
// to stderr (diagnostics only; read once).
inline bool kernel_debug() { static const bool on = std::getenv("MA_KERNEL_DEBUG") != nullptr; return on; }
[[noreturn]] inline void reject(const char* why) { throw std::domain_error(why); }
[[noreturn]] inline void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
inline void require(bool ok, const char* why) { if (!ok) throw std::invalid_argument(why); }

// An ordinary EOS calculation cannot decide a material/caloric boundary.
// The driver restarts through the strict reference and retains consumed attempts.
struct EosRefinement {};
// A flagged intermediate cannot be rounded without changing a representable
// result needed by the coupled solve. Restart through the validation oracle.
struct ArithmeticRefinement {};

// Error-free double-double accumulator for audits and reductions.
struct DD {
    double hi = 0, lo = 0;
    void add(double v) {
        const double s = hi + v, z = s - hi, e = (hi - (s - z)) + (v - z);
        const double t = lo + e, h = s + t;
        lo = t - (h - s); hi = h;
    }
    void add(const DD& o) { add(o.hi); add(o.lo); }
    [[nodiscard]] double value() const { return hi + lo; }
};

// Observe primary binary64 additions without changing the stored result.
// Each inexact write charges a half-ulp allowance to the interval; normal
// storage rounding remains inside its existing 128-epsilon operand bound.
struct PrimaryRounding {
    std::size_t count = 0;
    DD error, half_ulps;
    bool invalid = false;
    void addition(double before, double increment, double stored);
    void merge(const PrimaryRounding&);
    [[nodiscard]] double allowance() const;
    [[nodiscard]] bool within_bound(double audited_bound) const;
};

struct EnergyRounding {
    std::size_t high_count = 0;
    PrimaryRounding discarded;
    void observe(const MacPrimaryWrite&);
    void merge(const EnergyRounding&);
};

// ---------------------------------------------------------------- grid
struct Grid {
    std::size_t W = 0, H = 0, N = 0, XW = 0, YH = 0, NX = 0, NY = 0;
    bool px = false, py = false;
    double h = 1, depth = 1, V = 1, A = 1;
    std::vector<std::uint8_t> wall;
    // Face endpoints (cell index, or none for exterior/solid).
    std::vector<Index> xl, xr, yl, yr;
    // Closed faces between two open cells (MacGrid::closed_x/y): their
    // endpoints and inertia stay, but they are fixed like a wall face.
    std::vector<std::uint8_t> shut_x, shut_y;
    [[nodiscard]] bool closed(bool vertical, std::size_t f) const {
        const auto& s = vertical ? shut_y : shut_x; return !s.empty() && s[f] != 0;
    }
    // Rows per deterministic block; blocks partition cell rows, x-face rows
    // and y-face rows identically (the extra nonperiodic y-face row belongs
    // to the last block).
    std::size_t rows_per_block = 1, blocks = 1;
    explicit Grid(const MacGrid&);
    Grid() = default;
    [[nodiscard]] Index xface(std::size_t x, std::size_t y) const { return static_cast<Index>(x + y * XW); }
    [[nodiscard]] Index yface(std::size_t x, std::size_t y) const { return static_cast<Index>(x + y * W); }
    // Per-cell face table (left, right, bottom, top): avoids two runtime
    // integer divisions per lookup in every cell loop.
    std::vector<std::array<Index, 4>> cell_faces;
    [[nodiscard]] Index left_face(std::size_t c) const { return cell_faces[c][0]; }
    [[nodiscard]] Index right_face(std::size_t c) const { return cell_faces[c][1]; }
    [[nodiscard]] Index bottom_face(std::size_t c) const { return cell_faces[c][2]; }
    [[nodiscard]] Index top_face(std::size_t c) const { return cell_faces[c][3]; }
    [[nodiscard]] std::size_t block_row_begin(std::size_t b) const { return std::min(H, b * rows_per_block); }
    [[nodiscard]] std::size_t block_row_end(std::size_t b) const { return std::min(H, (b + 1) * rows_per_block); }
    // y-face rows [begin, end) of a block.
    [[nodiscard]] std::size_t block_yrow_end(std::size_t b) const { return b + 1 == blocks ? YH : block_row_end(b); }
};

// ---------------------------------------------------------------- model
struct Species {
    std::size_t registry = 0, profile = 0;
    bool gas = true;
    double molar_mass = 0, cv = 0, offset = 0, rho0 = 0, bulk = 0, maximum_ratio = 1, tmin = 0, tmax = 0;
    double conductivity = 0;
    double entropy_offset = 0; // a saturation calibration's constant specific entropy
    unsigned group = 0; // liquid phase group (SpeciesDefinition::liquid_phase_group)
};
struct Model {
    std::size_t registry_size = 0;
    std::vector<Species> species;      // present constituents, registry order
    std::vector<std::size_t> gases, liquids; // indices into species
    // Each species' liquid phase: the index into liquids of the first liquid
    // of its group (itself in group 0); none for gases.
    std::vector<std::size_t> phase_of;
    std::vector<std::uint64_t> passive_ids; // sorted
    [[nodiscard]] std::size_t K() const { return species.size(); }
    [[nodiscard]] std::size_t P() const { return passive_ids.size(); }
};

// ---------------------------------------------------------------- fields
// Primary production state: species-major masses m[k*N+i], U, passives
// q[p*N+i] and canonical face momentum.
// Void phase (MAC_VACUUM.md): Vo holds each cell's occupied material volume,
// zero for empty cells and walls; empty while every occupied cell fills it.
struct Fields {
    std::vector<double> m, U, U_remainder, q, jx, jy, Vo;
    [[nodiscard]] double* mass(std::size_t k, std::size_t N) { return m.data() + k * N; }
    [[nodiscard]] const double* mass(std::size_t k, std::size_t N) const { return m.data() + k * N; }
};
// The volume at which a cell's material is evaluated.
[[nodiscard]] inline double material_volume(const Fields& f, double grid_volume, std::size_t i) {
    return f.Vo.empty() ? grid_volume : f.Vo[i];
}

inline void prepare_primary_remainders(Fields& fields) {
    if (fields.U_remainder.empty()) fields.U_remainder.assign(fields.U.size(), 0);
    require(fields.U_remainder.size() == fields.U.size(), "Production primary remainder shape differs");
}
inline bool add_internal_energy(Fields& fields, std::size_t i, double increment, EnergyRounding& rounding) {
    const double before = fields.U[i], low = fields.U_remainder[i];
    const auto write = add_mac_primary(before, low, increment);
    rounding.observe(write); fields.U[i] = write.value; fields.U_remainder[i] = write.remainder;
    return increment != 0 && fields.U[i] == before && fields.U_remainder[i] == low;
}

// ---------------------------------------------------------------- EOS
enum Kind : std::uint8_t { empty = 0, gas_cell = 1, liquid_cell = 2, mixed_cell = 3, solid_cell = 4 };
struct Stats { double M = 0, Cv = 0, off = 0, nR = 0, Nl = 0; std::uint8_t kind = empty; };
struct State {
    double T = 0, p = 0, Vg = 0, Eel = 0, Ceq = 0, comp = 0, liquid_comp = 0;
};
// Per-cell EOS cache, valid for the stored masses/U at the recorded volume.
struct EosCache {
    std::vector<double> M, Cv, off, nR, Nl, T, p, Vg, Eel, Ceq, comp, lcomp;
    std::vector<std::uint8_t> kind;
    void resize(std::size_t n);
};
Stats cell_stats(const Model&, const Fields&, std::size_t N, std::size_t i);
// Returns false when the inventory is outside the EOS domain (no state).
bool evaluate_state(const Model&, const Fields&, std::size_t N, std::size_t i, const Stats&, double U,
    double volume, State& out);
// Liquid aggregates at pressure p.
struct LiquidAt { double contracted = 0, energy = 0, compliance = 0, volume = 0; };
LiquidAt liquid_at(const Model&, const Fields&, std::size_t N, std::size_t i, double p);
[[nodiscard]] double elastic_shape(double q); // log1p(q) - q/(1+q)

// Isentropic work from an anchor (stats and state at volume V0).
struct Anchor {
    double V0 = 0, p0 = 0, T0 = 0, Vg0 = 0, Cv = 0, nR = 0, k = 0, comp = 0;
    std::uint8_t kind = empty;
    // Single-liquid closed form: bulk modulus, nominal volume.
    double K = 0, Nl = 0, qmax = 0;
    bool single_liquid = false;
    // Void phase: a liquid may pass through tension during the root; the
    // active set then opens a void in it.
    bool tension = false;
};
// p1 is the endpoint pressure; dp1 = p1 - p0 without cancellation (general
// anchored branch only, which every gas/liquid cell takes).
struct Work { double dW = 0, derivative = 0, dU = 0, p1 = 0, dp1 = 0; };
// False when the trial leaves the EOS domain (void, compression limit).
bool isentropic_work(const Model&, const Fields&, std::size_t N, std::size_t i, const Anchor&, double dV, Work& out,
    bool robust_void_seed = false);

// ---------------------------------------------------------------- execution
class Pool {
public:
    explicit Pool(unsigned threads);
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    [[nodiscard]] unsigned size() const noexcept { return threads_; }
    // Runs job(block) for every block. Threads claim blocks dynamically, so
    // a descheduled thread never holds up the others; which thread runs a
    // block never changes its result. The exception of the lowest throwing
    // block is rethrown, independent of scheduling.
    void run(std::size_t blocks, const std::function<void(std::size_t)>& job);
private:
    void worker();
    void claim(std::uint64_t generation);
    unsigned threads_;
    std::vector<std::thread> workers_;
    std::mutex mutex_, run_mutex_;
    std::condition_variable start_;
    // Generation in the high 32 bits, next unclaimed block in the low bits.
    std::atomic<std::uint64_t> ticket_{0};
    std::atomic<std::size_t> done_{0};
    std::atomic<unsigned> sleeping_{0};
    std::atomic<bool> stop_{false};
    // Written only while the ticket is closed (see run()).
    std::atomic<std::size_t> blocks_{0};
    const std::function<void(std::size_t)>* job_ = nullptr;
    std::vector<std::exception_ptr> errors_;
};
Pool& pool_for(unsigned threads);

struct Exec {
    const Grid& g;
    Pool& pool;
    // job(block): process rows of one block.
    void blocks(const std::function<void(std::size_t)>& job) const { pool.run(g.blocks, job); }
    // Cell rows [y0,y1) of block b.
    template<class F> void rows(F&& f) const {
        pool.run(g.blocks, [&](std::size_t b) { f(g.block_row_begin(b), g.block_row_end(b), b); });
    }
    // Fixed column blocks [x0,x1) of a lattice with ncols columns (lines
    // along y). Column blocks depend only on the lattice width.
    template<class F> void columns(std::size_t ncols, F&& f) const {
        constexpr std::size_t width = 8;
        const auto nb = (ncols + width - 1) / width;
        pool.run(nb, [&](std::size_t b) { f(b * width, std::min(ncols, (b + 1) * width), b); });
    }
    template<class T, class F> std::vector<T> column_partials(std::size_t ncols, F&& f) const {
        constexpr std::size_t width = 8;
        const auto nb = (ncols + width - 1) / width;
        std::vector<T> part(nb);
        pool.run(nb, [&](std::size_t b) { part[b] = f(b * width, std::min(ncols, (b + 1) * width), b); });
        return part;
    }
    // Deterministic reduction of per-block partials in block order.
    template<class T, class F> std::vector<T> partials(F&& f) const {
        std::vector<T> part(g.blocks);
        pool.run(g.blocks, [&](std::size_t b) { part[b] = f(g.block_row_begin(b), g.block_row_end(b), b); });
        return part;
    }
};

// ---------------------------------------------------------------- stages
// Exact additive errors of flagged products/projections. Ordinary products
// never enter range arithmetic. Accepted shared parcels have one ledger entry,
// independent of the number of endpoints that use them; retained K is separate.
struct TransferRounding {
    std::size_t count = 0;
    MacEnergySum error;
    void product(double a, double b, double stored);
    void projection(MacEnergy value, double stored);
    void merge(const TransferRounding&);
    [[nodiscard]] MacEnergy bound() const;
    [[nodiscard]] bool within_bound() const;
};
// Dense lookup is allocated only after a flagged face appears. Clearing only
// those faces avoids a range-object pass on subsequent ordinary substeps.
// No physical state or continuation history is kept here.
struct FlaggedSweeps {
    std::vector<MacEnergy> values;
    std::vector<Index> faces;
    void reset(std::size_t size) {
        if (values.size() != size) values.clear();
        else for (const auto face : faces) values[face] = {};
        faces.clear();
    }
    void set(std::size_t size, Index face, MacEnergy value) {
        if (value.is_zero()) return;
        if (values.empty()) values.resize(size);
        values[face] = value; faces.push_back(face);
    }
    [[nodiscard]] const MacEnergy* data() const { return faces.empty() ? nullptr : values.data(); }
};
struct PhysicalAccounts {
    std::size_t accepted_substeps = 0;
    std::size_t conduction_stages = 0, conduction_contacts = 0, conduction_iterations = 0,
        unresolved_conduction_updates = 0;
    MacEnergySum conductive_redistribution;
    DD conductive_entropy;
    std::size_t viscosity_stages = 0, viscosity_modes = 0,
        unresolved_viscous_heat_updates = 0, unresolved_viscous_momentum_updates = 0;
    MacEnergySum viscous_heat, viscous_stored_heat, viscous_wall_x, viscous_wall_y, gravity_internal, viscous_entropy;
    void merge(const PhysicalAccounts&);
    [[nodiscard]] MacKernelPhysicalReport report() const;
};
struct Accounts {
    // Operand magnitudes for the 128-epsilon interval bound and counted
    // rounding allowances (per quantity: species, passives, jx, jy, energy).
    std::vector<double> operands, rounding;
    std::vector<TransferRounding> transfers;
    std::vector<PrimaryRounding> primary;
    std::vector<PrimaryRounding> inventory;
    EnergyRounding energy;
    double wall_x = 0, wall_y = 0, body_y = 0;
    double mixing_heat = 0, wall_heat = 0, compression_heat = 0, shock_heat = 0;
    // Concavity bound on the gravity reconciliation's entropy change, and its
    // operand magnitude sum |dU| / T for the audit allowance.
    DD gravity_entropy; double gravity_entropy_operands = 0;
    // Mass moved between phases by sharp-interface boiling, per quantity (the
    // audit's external term for each species), and its totals.
    std::vector<DD> conversion;
    double evaporated = 0, condensed = 0; std::size_t interface_conversions = 0;
    std::size_t subcell_void_events = 0;
    DD subcell_void_entropy;
    std::size_t unresolved = 0; // Accepted primary/parcel and existing pressure/heat writes.
    PhysicalAccounts physical;
    void resize(std::size_t quantities) {
        operands.assign(quantities, 0); rounding.assign(quantities, 0);
        transfers.assign(quantities, {});
        primary.assign(quantities, {});
        inventory.assign(quantities, {}); energy = {};
        conversion.assign(quantities, {});
    }
};
struct Counters {
    std::size_t multigrid_solves = 0, multigrid_cycles = 0, multigrid_fallbacks = 0, multigrid_levels = 0, topology_builds = 0;
    std::size_t newton = 0, linear = 0, line_search = 0, eos = 0, isentropic = 0, reconstruction_backtracks = 0,
        limited_momentum = 0, compression_events = 0, shock_events = 0, conduction_stages = 0, viscosity_modes = 0;
    double maximum_volume_closure = 0;
};
struct PressureMomentWorkspace {
    std::unique_ptr<PressureMomentMultigrid> coupled;
    bool coupled_ready = false;
    struct State {
        std::vector<double> a, b, mean, first, du, ra, rb;
        void resize(std::size_t n) { for (auto* v : {&a, &b, &mean, &first, &du, &ra, &rb}) v->resize(n); }
    } accepted, trial;
    std::array<std::vector<double>, 12> linear;
};
// Scratch arrays reused across substeps of one interval (no per-stage
// allocation after the first substep).
struct Workspace {
    std::unique_ptr<PressureMomentWorkspace> moments;
    // Derived topology and coefficients only. The active mask includes empty
    // cells; a changed mask or geometry replaces the hierarchy before use.
    std::shared_ptr<const interval_detail::PressureMultigridTopology> pressure_topology;
    std::unique_ptr<interval_detail::PressureMultigrid> pressure_multigrid;
    EosCache eos;
    std::vector<Anchor> anchor;
    std::vector<double> cell_mass, mass_after, ix, iy, ix1, iy1, inv_ix, inv_iy, dp0x, dp0y, wx, wy, bdiag;
    std::vector<double> dV, dW, cc, res, sx, sy, tdV, tdW, tcc, tres, sol, Vmat, dU, tdU;
    // Void phase: each cell's swept volume (material volume Vmat plus its
    // unchanged void), and the material and void parts of every x/y parcel and
    // retained remainder.
    std::vector<double> Vswept, filled, material_x, void_x, material_y, void_y, retained_material, retained_void;
    // Void phase: liquid surface cells (zero pressure, no pressure unknown) and
    // each rejoined surface cell's void at the substep start.
    std::vector<std::uint8_t> surface;
    std::vector<double> void0;
    std::vector<double> qx, qy, body_work_y;
    FlaggedSweeps full_qx, full_qy;
    std::vector<double> lr, lz, lp, lq, lAp, ldiag;
    // Transport: per-face parcel contents (F fields: K species, U, P
    // passives), per-cell retained contents, primal mass fluxes.
    std::vector<double> px, py, retained, fx, fy, theta;
    // Dual transport scratch.
    std::vector<double> keep, jsum, ktr, mix, wheat, v0, vprov, slope0, slope1, cour0, cour1, face_heat_x, face_heat_y;
    std::vector<double> dual_en, dual_et, dual_j1;
    std::vector<std::uint8_t> dual_has_n, dual_has_t;
    // Regularization scratch.
    std::vector<double> speed, pressure, sensor0, sensor1, closing, diffusion, cp, ceq, temperature, heat;
    std::vector<std::uint8_t> flag;
    // Reconstruction proxy (thermal + elastic energy) per cell.
    std::vector<double> proxy;
    // Sharp interfaces: per-cell phase-A/B fractions and phase energies (A is
    // all liquid beside gas, or the denser of exactly two liquids), each cell's
    // total liquid fraction, liquid-presence mask, denser liquid (an index into
    // Model::liquids, or none) and, with several liquids, each liquid's fraction.
    std::vector<double> phase_alpha, phase_gas, phase_liquid_energy, phase_gas_energy, phase_total, phase_liquid_fraction;
    std::vector<std::uint64_t> phase_liquids;
    std::vector<Index> phase_heavy;
    // Sharp-interface gravity: y-face PLIC half-cell weights at the substep start
    // and after transport, with their per-cell scratch.
    std::vector<double> gravity_weight_start, gravity_weight_end, gravity_alpha, gravity_total, gravity_lower, gravity_mass, gravity_fraction;
    std::vector<Index> gravity_heavy;
    std::vector<double> px_energy_remainder, py_energy_remainder, retained_energy_remainder;
    std::vector<std::uint8_t> two_phase;
    // Sharp-interface boiling (MAC_PHASE_CHANGE.md, "Sharp interfaces"): per
    // cell, the pair index + 1 (0: none), the held interface temperature, the
    // conduction heat the pressure stage converts at the interface, the
    // interface's integrated conductance and dT_sat/dp, and the pressure
    // stage's latent heat, converted mass and vapour volume.
    std::vector<std::uint8_t> interface_pair;
    std::vector<double> interface_temperature, interface_heat, interface_conductance, interface_slope;
    std::vector<double> interface_latent, interface_mass, interface_volume;
    // Per-cell EOS statistics depend only on masses: valid until transport
    // (or a retry) changes them.
    bool stats_valid = false;
    // Largest outgoing material volume over its CFL limit in the last
    // pressure/transport trial (MacStepControl::predictive); zero if none ran.
    double cfl_ratio = 0;
};
struct Context {
    Workspace& ws;
    const MacGrid& source_grid;
    const Model& model;
    const Grid& g;
    const MacClosedSettings& settings;
    const MacKernelOptions& options;
    Exec exec;
    Counters counters;
    MacKernelStageTiming timing;
    // Saturation pairs present in the model (species indices and EOS profile
    // indices) when phase change is enabled, with the EOS that defines them.
    struct PhasePair { std::size_t liquid = 0, vapour = 0, liquid_profile = 0, vapour_profile = 0; };
    const FluidThermodynamics* thermodynamics = nullptr;
    std::vector<PhasePair> phase_pairs{};
    [[nodiscard]] bool sharp_boiling() const { return !phase_pairs.empty() && settings.thermal_conduction; }
    // Quantity layout of Accounts: [0,K) species, [K,K+P) passives,
    // K+P jx, K+P+1 jy, K+P+2 energy.
    [[nodiscard]] std::size_t q_jx() const { return model.K() + model.P(); }
    [[nodiscard]] std::size_t q_jy() const { return model.K() + model.P() + 1; }
    [[nodiscard]] std::size_t q_energy() const { return model.K() + model.P() + 2; }
    [[nodiscard]] std::size_t quantities() const { return model.K() + model.P() + 3; }
};
// Face inertias (half masses of fluid neighbours) from cell masses.
void face_inertias(const Context&, const std::vector<double>& cell_mass, std::vector<double>& ix, std::vector<double>& iy);
void cell_masses(const Context&, const Fields&, std::vector<double>& out);
void evaluate_cells(Context&, const Fields&, EosCache&);

// Each stage updates the fields in place; a domain failure throws
// std::domain_error (the caller restores the substep source and retries).
void stage_viscosity(Context&, Fields&, double seconds, Accounts&);
void stage_conduction(Context&, Fields&, double seconds, Accounts&);
void stage_shock_heat(Context&, Fields&, double seconds, Accounts&);
void stage_compression(Context&, Fields&, double seconds, Accounts&);
void stage_pressure_transport(Context&, Fields&, double seconds, Accounts&);
void stage_pressure_transport_moments(Context&, Fields&, double seconds, Accounts&);
struct LinearSystem;
// Solve the anchor-linearized two-moment system. Compliance-weighted PCG
// applies its positive Schur polynomial, preconditioned by two fixed V-cycles.
void solve_pressure_moments_linear(Context&, const LinearSystem&, const std::vector<double>& compliance,
    const std::vector<double>& ra, const std::vector<double>& rb, std::vector<double>& da, std::vector<double>& db,
    double rtol, double atol);
// Reserved stage slot for gas diffusion (MAC_KERNEL.md, "Diffusion slot").
void stage_diffusion(Context&, Fields&, double seconds, Accounts&);
// Remap after the pressure step: reconstruction, primal parcels, dual
// momentum/K transport with the selected reconstruction, heat deposition and
// the gravity internal-energy adjustment. Reads ws.qx/qy/Vmat/body_work_y.
void transport_after_pressure(Context&, Fields&, double seconds, Accounts&);
// Void phase: each cell's material contact with its left, right, bottom and
// top faces (1 for filled cells), from its PLIC line. Empty while no cell is
// partly filled.
void void_apertures(Context&, const Fields&, std::vector<std::array<double, 4>>&);
// Void phase: a liquid-only partly filled cell resting on a wall, the domain
// edge or supporting material, which carries its whole weight on its bottom
// face (pressures from the EOS cache).
bool resting_surface(const Context&, const Fields&, const std::vector<double>& cell_mass, std::size_t cell);
// Sharp-interface gravity weight of every y face: the mass of its two half
// cells, splitting two-phase cells by their PLIC line (reference:
// mac_interface_gravity_weights). `cached` reads pressures from the EOS cache.
void interface_gravity_weights(Context&, const Fields&, bool cached, std::vector<double>& weights);

// Linear SPD solve (diag + weighted graph Laplacian) x = b on the cell grid.
struct LinearSystem {
    const Grid* g = nullptr;
    std::vector<double> diag, wx, wy; // wx[x face], wy[y face]: 0 for no coupling
    std::vector<std::uint8_t> active;   // 1 = unknown
};
struct LinearSolveResult { std::size_t iterations = 0; double residual = 0; bool converged = false; };
LinearSolveResult solve_linear(Context&, const LinearSystem&, const std::vector<double>& b, std::vector<double>& x,
    double relative_tolerance, double absolute_tolerance, std::size_t maximum_iterations);
}
