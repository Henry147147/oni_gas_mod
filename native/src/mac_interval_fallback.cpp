#include "mac_interval_fallback.hpp"
#include "mac_interval_audit.hpp"
#include <algorithm>

namespace ma::experimental {
void validate_mac_interval_fallback(const MacTransportState& before, const MacTransportState& after, MacClosedAdvanceReport& report) {
    const auto reconcile = [&](auto operands) {
        interval_detail::Account account; operands(account); const auto result = account.decide(operands);
        report.interval_audit_exact_fallbacks += result.exact ? 1U : 0U;
        if (!result.passed) throw std::domain_error("MAC reference fallback failed its interval inventory/impulse audit");
    };
    for (std::size_t s = 0; s < before.cells.front().mixture.masses.size(); ++s) reconcile([&](auto& account) {
        for (std::size_t i = 0; i < before.cells.size(); ++i) { account.add(after.cells[i].mixture.masses[s]); account.add(-before.cells[i].mixture.masses[s]); }
    });
    std::vector<std::uint64_t> ids;
    for (const auto* state : {&before, &after}) for (const auto& cell : state->cells)
        for (const auto& p : cell.passive_quantities) ids.push_back(p.identity);
    std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (const auto id : ids) reconcile([&](auto& account) {
        for (const auto* state : {&before, &after}) for (const auto& cell : state->cells) {
            const auto p = std::lower_bound(cell.passive_quantities.begin(), cell.passive_quantities.end(), id,
                [](const auto& entry, auto identity) { return entry.identity < identity; });
            if (p != cell.passive_quantities.end() && p->identity == id) account.add((state == &before ? -1 : 1) * p->quantity);
        }
    });
    for (int a = 0; a < 2; ++a) {
        const auto j0 = a == 0 ? mac_field_values(before.momentum_x, before.full_momentum_x) : mac_field_values(before.momentum_y, before.full_momentum_y);
        const auto j1 = a == 0 ? mac_field_values(after.momentum_x, after.full_momentum_x) : mac_field_values(after.momentum_y, after.full_momentum_y);
        reconcile([&](auto& account) {
            for (std::size_t f = 0; f < j0.size(); ++f) { account.add(j1[f]); account.add(j0[f].negated()); }
            account.add((a == 0 ? report.full_wall_impulse_x : report.full_wall_impulse_y).negated());
            if (a == 1) account.add(report.full_body_impulse_y.negated());
        });
    }
    // The gravity reconciliation is credited by its concavity bound; every
    // other stage must produce entropy.
    if (report.entropy_change_j_per_k - report.gravity_entropy_bound_j_per_k < -report.entropy_roundoff_j_per_k)
        throw std::domain_error("MAC reference fallback failed its interval entropy audit");
}
}
