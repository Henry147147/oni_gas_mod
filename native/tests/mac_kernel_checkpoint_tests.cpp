// Persisted production selection: ordinary restore/advance must continue the
// same kernel, pressure setting and diffusion composition without caller flags.
#include "mac_kernel_fixture.hpp"
#include <bit>
#include <filesystem>
#include <fstream>

using namespace ma::testing;
using namespace ma::testing::kernel;

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void seal(std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i + 8 < bytes.size(); ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    put(bytes, bytes.size() - 8, hash, 8);
}
std::size_t kernel_offset(const std::vector<std::uint8_t>& bytes) {
    std::size_t model_bytes = 0;
    for (std::size_t i = 0; i < 4; ++i) model_bytes |= static_cast<std::size_t>(bytes.at(12 + i)) << (8 * i);
    return 16 + model_bytes + 103;
}
void rejects(const std::vector<std::uint8_t>& bytes) {
    const auto& s = setup(); bool rejected = false;
    try { static_cast<void>(MacClosedWorld::from_checkpoint(s.registry, s.eos, bytes)); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Malformed production checkpoint was admitted");
}
std::vector<std::uint8_t> legacy_image(int id) {
    const auto& s = setup(); auto settings = MacClosedSettings{};
    if (id == 1 || id == 5) settings.viscosity = {.1, .2};
    if (id >= 3) settings.diffusion = {id == 3 ? MacDiffusionMode::physical : MacDiffusionMode::accelerated, id == 3 ? 1. : 100.};
    auto bytes = MacClosedWorld(s.registry, s.eos, s.state(4, 3, false, false, true), settings).checkpoint();
    // Synthesize the historical wire selector without constructing a retired
    // owner. These six images must still match the pre-retirement writer.
    if (id == 2) {
        const auto viscosity_at = kernel_offset(bytes) - 16;
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(viscosity_at), 20, 0);
        put(bytes, 8, 4, 4); put(bytes, kernel_offset(bytes), 1, 4); seal(bytes);
    } else if (id >= 4) { put(bytes, kernel_offset(bytes), 1, 4); seal(bytes); }
    return bytes;
}
void continuation(Scene scene, MacDiffusionSettings diffusion, PressurePreconditioner preconditioner) {
    const auto& s = setup();
    scene.settings.preconditioner = preconditioner; scene.settings.diffusion = diffusion;
    const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings);
    const auto held = source.checkpoint();
    const auto first = source.advanced(.002, 4096, MacExecutionPolicy::production);
    const auto image = first.world.checkpoint();
    check(image[8] == ma::testing::checkpoint_version(first.world,6) && first.world.settings().numerical_kernel == MacNumericalKernel::production,
        "Production request did not persist the kernel selection");
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, image);
    check(restored.checkpoint() == image && restored.settings().preconditioner == preconditioner &&
        restored.settings().diffusion.mode == diffusion.mode && restored.settings().diffusion.multiplier == diffusion.multiplier,
        "Production checkpoint lost a numerical/physical setting");
    auto options = mac_kernel_default_options(); options.threads = 1; set_mac_kernel_default_options(options);
    const auto expected = first.world.advanced(.002, 4096, MacExecutionPolicy::production);
    options.threads = 12; set_mac_kernel_default_options(options);
    const auto resumed = restored.advanced(.002, 4096); // No policy supplied.
    if (scene.state.cells.size() > 32768)
        check(last_mac_production_report().threads_used == 12, "Large ordinary restore did not execute with twelve workers");
    options.threads = 1; set_mac_kernel_default_options(options);
    check(resumed.world.checkpoint() == expected.world.checkpoint() && source.checkpoint() == held,
        "Ordinary restore/advance changed the selected trajectory or source");
    check(resumed.report.interval_kernel_intervals == 1 && resumed.report.interval_reference_fallbacks == 0,
        "Restored production owner silently advanced the reference");
    if (preconditioner == PressurePreconditioner::jacobi)
        check(resumed.report.interval_multigrid_solves == 0, "Restored production owner ignored saved Jacobi");
    std::cout << "PASS production continuation " << scene.name << " diffusion=" << static_cast<unsigned>(diffusion.mode)
        << " preconditioner=" << static_cast<unsigned>(preconditioner) << '\n';
}
}
int main(int argc, char** argv) {
    try {
        const auto& s = setup();
        // The comparison harness checks every byte against images written by
        // the pre-retirement version-2/3/4/5 writer.
        if (argc == 3 && std::string(argv[1]) == "--write-legacy") {
            const std::filesystem::path directory(argv[2]); std::filesystem::create_directories(directory);
            for (int id = 0; id < 6; ++id) {
                const auto bytes = legacy_image(id);
                std::ofstream out(directory / (std::to_string(id) + ".mac"), std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                check(static_cast<bool>(out), "Cannot write legacy checkpoint control");
            }
            return 0;
        }
        check(argc == 1, "Unknown production checkpoint test arguments");
        for (const auto preconditioner : {PressurePreconditioner::automatic, PressurePreconditioner::jacobi}) {
            for (auto scene : {wave(19, 17), room(24, 16), liquid(8, 8), mixed(12, 12), vacuum(16, 8)})
                continuation(std::move(scene), MacDiffusionSettings::disabled(), preconditioner);
            for (const auto diffusion : {MacDiffusionSettings{}, MacDiffusionSettings{MacDiffusionMode::accelerated, 100}})
                continuation(wave(19, 17), diffusion, preconditioner);
        }
        // The small cases cap the pool at one worker. This odd periodic grid
        // executes twelve workers during the ordinary restored advance.
        continuation(wave(193, 173), MacDiffusionSettings::disabled(), PressurePreconditioner::automatic);
        // The equilibrium certificate must persist selection too.
        const MacClosedWorld rest(s.registry, s.eos, s.state(8, 8, true));
        const auto certified = rest.advanced(.1, 64, MacExecutionPolicy::production);
        const auto cert_image = certified.world.checkpoint();
        const auto cert_restore = MacClosedWorld::from_checkpoint(s.registry, s.eos, cert_image);
        check(cert_image[8] == 6 && certified.report.exact_equilibrium_intervals == 1 &&
            cert_restore.advanced(.1).world.checkpoint() == certified.world.advanced(.1).world.checkpoint(),
            "Certified production checkpoint lost exact continuation");
        for (int id = 0; id < 6; ++id) {
            const auto legacy = legacy_image(id);
            if (id == 2 || id >= 4) {
                bool retired = false;
                try { static_cast<void>(MacClosedWorld::from_checkpoint(s.registry, s.eos, legacy)); }
                catch (const std::invalid_argument& e) { retired = std::string(e.what()).find("retired") != std::string::npos; }
                check(retired, "Legacy kernel-1 image was not explicitly rejected as retired");
            } else check(MacClosedWorld::from_checkpoint(s.registry, s.eos, legacy).checkpoint() == legacy,
                "Supported legacy reference image changed bytes");
        }
        auto retired_settings = MacClosedSettings{}; retired_settings.numerical_kernel = MacNumericalKernel::retired_interval_audited;
        bool retired = false;
        try { retired_settings.validate(); } catch (const std::invalid_argument&) { retired = true; }
        check(retired, "Retired kernel selection was admitted by the owner");
        std::cout << "PASS legacy reference bytes and explicit retirement of version-4/5 kernel 1\n";
        // Well-checksummed malformed values must reach semantic admission.
        const auto offset = kernel_offset(cert_image);
        for (const auto value : {0U, 1U, 3U, 99U, UINT32_MAX}) {
            auto bad = cert_image; put(bad, offset, value, 4); seal(bad); rejects(bad);
        }
        for (const auto version : {1U, 2U, 3U, 4U, 5U, 7U, 99U}) {
            auto bad = cert_image; put(bad, 8, version, 4); seal(bad); rejects(bad);
        }
        auto bad = cert_image; put(bad, offset + 4, 99, 4); seal(bad); rejects(bad);
        bad = cert_image; put(bad, offset + 8, std::bit_cast<std::uint64_t>(2.), 8); seal(bad); rejects(bad);
        bad = cert_image; bad.insert(bad.end() - 8, 0); seal(bad); rejects(bad);
        for (std::size_t length = 0; length < cert_image.size(); ++length)
            rejects(std::vector<std::uint8_t>(cert_image.begin(), cert_image.begin() + static_cast<std::ptrdiff_t>(length)));
        // Unsupported selectors keep their physical laws through an audited
        // reference interval, while retaining selection 2 for the next step.
        auto scene = wave(8, 8); scene.settings.numerical_kernel = MacNumericalKernel::production;
        scene.settings.preconditioner = PressurePreconditioner::incomplete_cholesky;
        const MacClosedWorld selected(s.registry, s.eos, scene.state, scene.settings);
        const auto fallback = selected.advanced(.002);
        scene.settings.numerical_kernel = MacNumericalKernel::reference;
        const auto reference = MacClosedWorld(s.registry, s.eos, scene.state, scene.settings).advanced(.002);
        check(fallback.report.interval_reference_fallbacks == 1 && fallback.world.settings().numerical_kernel == MacNumericalKernel::production &&
            fingerprint(fallback.world.state()) == fingerprint(reference.world.state()), "Saved selector fallback changed the selected physical laws");
        // A supported source can cross the normal-range boundary after doing
        // work. Its discarded attempt must still consume the shared budget.
        auto ranged = wave(8, 8); ranged.settings.numerical_kernel = MacNumericalKernel::production;
        for (auto& cell : ranged.state.cells) cell.passive_quantities.front().quantity = 0x1p450;
        const MacClosedWorld ranged_source(s.registry, s.eos, ranged.state, ranged.settings);
        const auto ranged_held = ranged_source.checkpoint(); bool exhausted = false;
        try { static_cast<void>(ranged_source.advanced(.00001, 1)); }
        catch (const std::runtime_error&) { exhausted = true; }
        check(exhausted && ranged_source.checkpoint() == ranged_held, "Mid-interval fallback reset its budget or changed the source");
        const auto ranged_after = ranged_source.advanced(.00001, 2);
        ranged.settings.numerical_kernel = MacNumericalKernel::reference;
        const auto ranged_reference = MacClosedWorld(s.registry, s.eos, ranged.state, ranged.settings).advanced(.00001, 1);
        check(last_mac_production_report().reference_fallback && ranged_after.report.attempts == 2 && ranged_after.report.rejected_attempts == 1 &&
            ranged_after.report.interval_reference_fallbacks == 1 && ranged_after.report.interval_audit_exact_fallbacks > 0 &&
            ranged_after.world.settings().numerical_kernel == MacNumericalKernel::production &&
            fingerprint(ranged_after.world.state()) == fingerprint(ranged_reference.world.state()),
            "Mid-interval fallback lost its selection, audit, consumed attempt or reference physics");
        std::cout << "PASS certified continuation, version/selection/diffusion guards, truncation and selected reference fallback\n";
        std::cout << "PASS mid-interval range fallback, exact audit and shared-budget rollback\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
