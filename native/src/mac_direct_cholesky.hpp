#pragma once
// Sparse Cholesky of a game pressure system (MAC_GAME_KERNEL.md, "Large
// pressure and heat components"): geometric nested dissection on the cell
// grid orders the unknowns, Liu's elimination tree and the rows' subtrees
// give the factor's pattern, and an up-looking factorisation fills it.
// Written for this solver from the textbook algorithms. Single-threaded,
// every operation in a fixed order: a result depends on the system only.
// Tested by ma_mac_direct_cholesky_tests against a dense factorisation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ma::experimental::mac_kernel::direct {
inline constexpr auto absent = ~std::size_t{0};
// Symmetric graph without self loops, CSR, with the order of its nodes.
struct Graph {
    std::vector<std::size_t> offsets, columns;
    std::vector<std::array<std::size_t, 2>> at; // grid coordinates (x, y)
    [[nodiscard]] std::size_t size() const { return at.size(); }
};
// Orders the nodes: recursively, the grid line (row or column) with the
// fewest nodes that leaves at least a fifth on either side separates the
// rest, which is ordered first. A part of at most `leaf` nodes keeps its
// given order.
inline std::vector<std::size_t> nested_dissection(const Graph& g, std::size_t leaf = 48) {
    std::vector<std::size_t> order, all(g.size()), count;
    order.reserve(g.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    const auto split = [&](const auto& self, std::vector<std::size_t>& cells) -> void {
        if (cells.size() <= leaf) { order.insert(order.end(), cells.begin(), cells.end()); return; }
        std::array<std::size_t, 2> lo = g.at[cells[0]], hi = lo;
        for (const auto c : cells)
            for (std::size_t a = 0; a < 2; ++a) { lo[a] = std::min(lo[a], g.at[c][a]); hi[a] = std::max(hi[a], g.at[c][a]); }
        std::size_t best_axis = 2, best_line = 0;
        double best = 0;
        for (std::size_t a = 0; a < 2; ++a) {
            const auto extent = hi[a] - lo[a] + 1;
            if (extent < 3) continue;
            count.assign(extent, 0);
            for (const auto c : cells) ++count[g.at[c][a] - lo[a]];
            std::size_t below = count[0];
            const auto total = cells.size();
            for (std::size_t k = 1; k + 1 < extent; below += count[k], ++k) {
                const auto above = total - below - count[k];
                if (5 * below < total || 5 * above < total) continue;
                const double imbalance = std::abs(static_cast<double>(below) - static_cast<double>(above)) / static_cast<double>(total);
                const double score = static_cast<double>(count[k]) * (1 + imbalance);
                if (best_axis == 2 || score < best) { best = score; best_axis = a; best_line = lo[a] + k; }
            }
        }
        if (best_axis == 2) { order.insert(order.end(), cells.begin(), cells.end()); return; }
        std::vector<std::size_t> left, right, separator;
        for (const auto c : cells) {
            const auto v = g.at[c][best_axis];
            (v < best_line ? left : v > best_line ? right : separator).push_back(c);
        }
        cells.clear(); cells.shrink_to_fit();
        self(self, left);
        self(self, right);
        order.insert(order.end(), separator.begin(), separator.end());
    };
    split(split, all);
    return order;
}
// The pattern of the factor of the permuted system.
struct Symbolic {
    std::size_t n = 0;
    std::vector<std::size_t> perm, inverse, parent; // perm[new] = old; elimination tree
    std::vector<std::size_t> Lp;                     // column starts; diagonal first
    std::vector<std::size_t> upper_offsets, upper;   // per new row k: CSR positions of g with new index < k
    double flops = 0;                                // sum of squared column counts: the factor's work
};
inline Symbolic analyse(const Graph& g, std::vector<std::size_t> perm) {
    Symbolic s;
    s.n = g.size(); s.perm = std::move(perm); s.inverse.assign(s.n, 0);
    for (std::size_t k = 0; k < s.n; ++k) s.inverse[s.perm[k]] = k;
    s.upper_offsets.assign(s.n + 1, 0);
    for (std::size_t k = 0; k < s.n; ++k) {
        const auto old = s.perm[k];
        for (auto p = g.offsets[old]; p < g.offsets[old + 1]; ++p)
            if (s.inverse[g.columns[p]] < k) s.upper.push_back(p);
        s.upper_offsets[k + 1] = s.upper.size();
    }
    s.parent.assign(s.n, absent);
    std::vector<std::size_t> ancestor(s.n, absent);
    for (std::size_t k = 0; k < s.n; ++k)
        for (auto q = s.upper_offsets[k]; q < s.upper_offsets[k + 1]; ++q)
            for (auto i = s.inverse[g.columns[s.upper[q]]]; i != absent && i < k;) {
                const auto next = ancestor[i];
                ancestor[i] = k;
                if (next == absent) { s.parent[i] = k; break; }
                i = next;
            }
    std::vector<std::size_t> counts(s.n, 1), mark(s.n, absent);
    for (std::size_t k = 0; k < s.n; ++k) {
        mark[k] = k;
        for (auto q = s.upper_offsets[k]; q < s.upper_offsets[k + 1]; ++q)
            for (auto i = s.inverse[g.columns[s.upper[q]]]; mark[i] != k; i = s.parent[i]) { ++counts[i]; mark[i] = k; }
    }
    s.Lp.assign(s.n + 1, 0);
    for (std::size_t j = 0; j < s.n; ++j) {
        s.Lp[j + 1] = s.Lp[j] + counts[j];
        s.flops += static_cast<double>(counts[j]) * static_cast<double>(counts[j]);
    }
    return s;
}
struct Factor {
    std::vector<double> Lx, x;
    std::vector<std::size_t> Li, stack, mark, fill;
};
// diagonal[old], off[CSR position] = A(old row, old column) off the diagonal.
// False on a pivot that is not positive and finite.
inline bool factor(const Symbolic& s, const Graph& g, const std::vector<double>& diagonal, const std::vector<double>& off, Factor& f) {
    f.Lx.assign(s.Lp.back(), 0); f.Li.assign(s.Lp.back(), 0);
    f.x.assign(s.n, 0); f.mark.assign(s.n, absent); f.fill.assign(s.n, 0); f.stack.resize(s.n);
    for (std::size_t k = 0; k < s.n; ++k) {
        auto top = s.n;
        f.mark[k] = k;
        double pivot = diagonal[s.perm[k]];
        for (auto q = s.upper_offsets[k]; q < s.upper_offsets[k + 1]; ++q) {
            const auto p = s.upper[q];
            const auto i = s.inverse[g.columns[p]];
            f.x[i] += off[p];
            std::size_t length = 0;
            for (auto j = i; f.mark[j] != k; j = s.parent[j]) { f.stack[length++] = j; f.mark[j] = k; }
            while (length > 0) f.stack[--top] = f.stack[--length];
        }
        for (; top < s.n; ++top) {
            const auto i = f.stack[top];
            const double value = f.x[i] / f.Lx[s.Lp[i]];
            f.x[i] = 0;
            for (auto p = s.Lp[i] + 1; p < s.Lp[i] + f.fill[i]; ++p) f.x[f.Li[p]] -= f.Lx[p] * value;
            pivot -= value * value;
            const auto at = s.Lp[i] + f.fill[i]++;
            f.Li[at] = k; f.Lx[at] = value;
        }
        if (!(pivot > 0) || !std::isfinite(pivot)) return false;
        f.Li[s.Lp[k]] = k; f.Lx[s.Lp[k]] = std::sqrt(pivot); f.fill[k] = 1;
    }
    return true;
}
// out[old] = A^-1 b[old]; `work` is scratch.
inline void solve(const Symbolic& s, const Factor& f, const std::vector<double>& b, std::vector<double>& out, std::vector<double>& work) {
    work.resize(s.n);
    for (std::size_t k = 0; k < s.n; ++k) work[k] = b[s.perm[k]];
    for (std::size_t j = 0; j < s.n; ++j) {
        work[j] /= f.Lx[s.Lp[j]];
        for (auto p = s.Lp[j] + 1; p < s.Lp[j + 1]; ++p) work[f.Li[p]] -= f.Lx[p] * work[j];
    }
    for (std::size_t j = s.n; j-- > 0;) {
        double value = work[j];
        for (auto p = s.Lp[j] + 1; p < s.Lp[j + 1]; ++p) value -= f.Lx[p] * work[f.Li[p]];
        work[j] = value / f.Lx[s.Lp[j]];
    }
    out.resize(s.n);
    for (std::size_t k = 0; k < s.n; ++k) out[s.perm[k]] = work[k];
}
}
