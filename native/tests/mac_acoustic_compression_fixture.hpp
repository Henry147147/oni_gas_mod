#pragma once
#include "mac_compression_fixture.hpp"

namespace ma::testing {
inline void export_acoustic_pair(std::size_t id, const MacCompressionPair& p) {
    export_compression_pair(id, p);
    std::cout << "ALINEAR " << id;
    for (const auto& v : {p.linear_speed, p.linear_relaxation_number, p.linear_relaxation_phi}) export_compression_scaled(v);
    std::cout << '\n';
}
inline void export_acoustic_compression(std::size_t id, const MacTransportState& source,
    const MacCompressionAdvance& result, double dt) {
    export_compression(id, source, result, dt);
    for (std::size_t i = 0; i < result.report.source_linear_speeds.size(); ++i) {
        std::cout << "ASPEED " << id << ' ' << i; export_compression_scaled(result.report.source_linear_speeds[i]); std::cout << '\n';
    }
    for (std::size_t i = 0; i < result.report.events.size(); ++i) {
        const auto& p = result.report.events[i].pair; std::cout << "ALINEAR " << 1000 + id * 100 + i;
        for (const auto& v : {p.linear_speed, p.linear_relaxation_number, p.linear_relaxation_phi}) export_compression_scaled(v);
        std::cout << '\n';
    }
}
}
