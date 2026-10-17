#pragma once
#include "mac_compression_fixture.hpp"

namespace ma::testing {
inline void export_acoustic_pair(std::size_t id, const MacCompressionPair& p, std::string_view prefix = {}) {
    export_compression_pair(id, p, prefix);
    std::cout << prefix << "ALINEAR " << id;
    for (const auto& v : {p.linear_speed, p.linear_relaxation_number, p.linear_relaxation_phi}) export_compression_scaled(v);
    std::cout << '\n';
}
inline void export_acoustic_compression(std::size_t id, const MacTransportState& source,
    const MacCompressionAdvance& result, double dt, std::string_view prefix = {}) {
    export_compression(id, source, result, dt, prefix);
    for (std::size_t i = 0; i < result.report.source_linear_speeds.size(); ++i) {
        std::cout << prefix << "ASPEED " << id << ' ' << i; export_compression_scaled(result.report.source_linear_speeds[i]); std::cout << '\n';
    }
    for (std::size_t i = 0; i < result.report.source_pressures_pa.size(); ++i) {
        std::cout << prefix << "PSTATE " << id << ' ' << i << ' ' << result.report.source_pressures_pa[i] << '\n';
        for (std::size_t axis = 0; axis < 2; ++axis) {
            std::cout << prefix << "PSENSOR " << id << ' ' << axis << ' ' << i;
            export_compression_scaled(result.report.source_pressure_sensors[axis][i]); std::cout << '\n';
        }
    }
    for (std::size_t i = 0; i < result.report.events.size(); ++i) {
        const auto& p = result.report.events[i].pair; std::cout << prefix << "ALINEAR " << 1000 + id * 100 + i;
        for (const auto& v : {p.linear_speed, p.linear_relaxation_number, p.linear_relaxation_phi}) export_compression_scaled(v);
        std::cout << '\n';
    }
}
}
