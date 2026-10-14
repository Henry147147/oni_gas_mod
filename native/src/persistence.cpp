#include "mixed_atmospheres/grid.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace ma {
namespace {
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
constexpr std::uint32_t schema_version = 1;
constexpr char magic[] = "MIXGAS01";
std::uint64_t checksum(std::span<const std::uint8_t> data) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : data) { value ^= byte; value *= 1099511628211ULL; }
    return value;
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void u8(std::uint8_t value) { bytes.push_back(value); }
    void u32(std::uint32_t value) { for (int shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>(value >> shift)); }
    void u64(std::uint64_t value) { for (int shift = 0; shift < 64; shift += 8) u8(static_cast<std::uint8_t>(value >> shift)); }
    void number(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void string(const std::string& value) {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position = 0;
    std::uint8_t u8() {
        if (position == bytes.size()) throw std::invalid_argument("Truncated save");
        return bytes[position++];
    }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    double number() {
        const double value = std::bit_cast<double>(u64());
        require_finite(value, "saved number");
        return value;
    }
    bool flag() {
        const auto value = u8();
        if (value > 1) throw std::invalid_argument("Invalid saved boolean");
        return value != 0;
    }
    std::string string() {
        const auto size = u32();
        if (size > 256 || size > bytes.size() - position) throw std::invalid_argument("Invalid saved identifier");
        const std::string result(reinterpret_cast<const char*>(bytes.data() + position), size);
        position += size;
        return result;
    }
};

void write_species(Writer& w, const SpeciesDefinition& s) {
    w.string(s.id); w.u32(static_cast<std::uint32_t>(s.phase));
    for (double value : {s.molar_mass, s.cp, s.density, s.conductivity, s.formation_enthalpy, s.max_mole_fraction}) w.number(value);
    w.u32(static_cast<std::uint32_t>(s.atoms.size()));
    for (const auto& [atom, count] : s.atoms) { w.string(atom); w.number(count); }
}
void check_species(Reader& reader, const SpeciesDefinition& s) {
    if (reader.u32() != static_cast<std::uint32_t>(s.phase)) throw std::invalid_argument("Saved phase definition changed: " + s.id);
    for (double value : {s.molar_mass, s.cp, s.density, s.conductivity, s.formation_enthalpy, s.max_mole_fraction})
        if (reader.number() != value) throw std::invalid_argument("Saved species properties changed: " + s.id);
    if (reader.u32() != s.atoms.size()) throw std::invalid_argument("Saved species composition changed");
    for (const auto& [atom, count] : s.atoms) {
        if (reader.string() != atom || reader.number() != count) throw std::invalid_argument("Saved species composition changed");
    }
}
}

std::vector<std::uint8_t> GasWorld::save() const {
    Writer w;
    for (std::size_t i = 0; i < 8; ++i) w.u8(static_cast<std::uint8_t>(magic[i]));
    w.u32(schema_version);
    w.u32(static_cast<std::uint32_t>(registry_.size()));
    for (const auto& s : registry_.all()) write_species(w, s);
    w.u32(static_cast<std::uint32_t>(width_)); w.u32(static_cast<std::uint32_t>(height_));
    w.number(dx_); w.number(depth_);
    w.number(settings_.gravity); w.number(settings_.diffusivity);
    w.u8(settings_.conduction ? 1 : 0); w.u8(settings_.open_top ? 1 : 0);
    w.number(time_); w.number(escaped_energy_);
    for (double mass : escaped_masses_) w.number(mass);
    for (const auto& c : cells_) {
        w.u8(c.wall ? 1 : 0);
        w.number(c.momentum_x); w.number(c.momentum_y); w.number(c.mixture.energy);
        for (double mass : c.mixture.masses) w.number(mass);
    }
    w.u64(checksum(w.bytes));
    return w.bytes;
}

GasWorld GasWorld::load(std::span<const std::uint8_t> bytes, SpeciesRegistry registry) {
    if (bytes.size() < 24 || bytes.size() > 1024ULL * 1024 * 1024) throw std::invalid_argument("Invalid save size");
    Reader trailer{bytes.last(8)};
    if (trailer.u64() != checksum(bytes.first(bytes.size() - 8))) throw std::invalid_argument("Save checksum mismatch");
    Reader r{bytes.first(bytes.size() - 8)};
    for (std::size_t i = 0; i < 8; ++i) if (r.u8() != static_cast<std::uint8_t>(magic[i])) throw std::invalid_argument("Wrong save format");
    if (r.u32() != schema_version) throw std::invalid_argument("Unsupported save schema");
    const auto count = r.u32();
    if (count != registry.size()) throw std::invalid_argument("Saved species registry mismatch");
    std::vector<std::size_t> mapping;
    std::set<std::size_t> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = registry.index(r.string());
        if (!seen.insert(index).second) throw std::invalid_argument("Duplicate saved species");
        mapping.push_back(index);
        check_species(r, registry[index]);
    }
    const auto width = r.u32(), height = r.u32();
    const double dx = r.number(), depth = r.number();
    SolverSettings settings;
    settings.gravity = r.number(); settings.diffusivity = r.number();
    settings.conduction = r.flag(); settings.open_top = r.flag();
    const double time = r.number(), escaped_energy = r.number();
    if (time < 0) throw std::invalid_argument("Negative simulation time");
    const std::uint64_t cell_count = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t required = 8ULL * count + cell_count * (25ULL + 8ULL * count);
    if (required != r.bytes.size() - r.position) throw std::invalid_argument("Saved dimensions do not match payload");
    GasWorld world(width, height, dx, depth, settings, std::move(registry));
    world.time_ = time; world.escaped_energy_ = escaped_energy;
    for (std::size_t i = 0; i < count; ++i) {
        const double m = r.number();
        if (m < 0) throw std::invalid_argument("Negative escaped mass");
        world.escaped_masses_[mapping[i]] = m;
    }
    for (auto& c : world.cells_) {
        c.wall = r.flag(); c.momentum_x = r.number(); c.momentum_y = r.number(); c.mixture.energy = r.number();
        for (std::size_t i = 0; i < count; ++i) c.mixture.masses[mapping[i]] = r.number();
        c.mixture.validate(world.registry_);
        for (std::size_t i = 0; i < count; ++i)
            if (c.mixture.masses[i] != 0 && world.registry_[i].phase != Phase::gas)
                throw std::invalid_argument("Saved reference gas world contains condensed material");
        if ((c.wall || c.mixture.mass() == 0) &&
            (c.mixture.mass() != 0 || c.momentum_x != 0 || c.momentum_y != 0 || c.mixture.energy != 0))
            throw std::invalid_argument("Wall/vacuum contains material, momentum, or energy");
        require_finite(c.kinetic_energy(), "saved kinetic energy");
    }
    return world;
}
}
