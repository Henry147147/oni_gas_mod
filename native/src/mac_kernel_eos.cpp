// Production EOS: the reference ideal-gas / zero-expansion elastic-liquid
// model (native/experimental/liquid_reference.cpp) in binary64, with closed
// forms for gas-only and single-liquid cells and safeguarded Newton roots for
// gas/liquid and multi-liquid cells. Increments from an anchor are formed
// without subtracting absolute pressures (MAC_KERNEL.md, "Pressure").
#include "mac_kernel_detail.hpp"

namespace ma::experimental::mac_kernel {
namespace {
constexpr double R = experimental::reference_gas_constant;
double boundary(double value, double bound) { return 32 * eps * std::max(std::abs(value), std::abs(bound)); }
// sigma(s) = (log1p(s) - s/(1+s)) / s^2 and its derivative, |s| < 1e-3.
void shape_series(double s, double& sigma, double& derivative) {
    double power = 1, value = 0, slope = 0;
    for (int n = 2; n <= 13; ++n) {
        const double c = ((n % 2) == 0 ? 1. : -1.) * static_cast<double>(n - 1) / n;
        value += c * power;
        if (n > 2) slope += c * static_cast<double>(n - 2) * (power / s);
        power *= s;
    }
    sigma = value; derivative = slope;
}
// (x - log1p(x)) / x^2 (reference logarithmic_second_difference).
double log_second_difference(double x) {
    if (std::abs(x) >= 1e-3) return (1 - std::log1p(x) / x) / x;
    double power = 1, sum = .5;
    for (int n = 3; n <= 14; ++n) { power *= x; sum += (n % 2 == 0 ? 1 : -1) * power / n; }
    return sum;
}
double exprel(double x) { return x == 0 ? 1 : std::expm1(x) / x; }
// [expm1(l) - expm1(a l)/a] / [(1-a) l^2] (reference gas_second_difference).
double gas_second_difference(double l, double a, double b) {
    if (std::abs(l) >= 1e-3) return (std::exp(a * l) * exprel(b * l) - exprel(a * l)) / l;
    double geometric = 1, power = 1, factorial = 2, sum = .5;
    for (int n = 3; n <= 16; ++n) { geometric = 1 + a * geometric; power *= l; factorial *= n; sum += geometric * power / factorial; }
    return sum;
}
}
double elastic_shape(double q) {
    if (q == 0) return 0;
    if (std::abs(q) < 1e-3) {
        double power = q, factor = .5;
        for (int n = 3; n <= 12; ++n) { factor += ((n % 2) == 0 ? 1 : -1) * (static_cast<double>(n - 1) / n) * power; power *= q; }
        return q * (q * factor);
    }
    return std::log1p(q) - q / (1 + q);
}
void EosCache::resize(std::size_t n) {
    for (auto* v : {&M, &Cv, &off, &nR, &Nl, &T, &p, &Vg, &Eel, &Ceq, &comp, &lcomp}) v->assign(n, 0);
    kind.assign(n, empty);
}
Stats cell_stats(const Model& model, const Fields& f, std::size_t N, std::size_t i) {
    Stats s; double moles = 0;
    for (std::size_t k = 0; k < model.K(); ++k) {
        const double m = f.m[k * N + i]; if (m == 0) continue;
        const auto& sp = model.species[k];
        s.M += m; s.Cv += m * sp.cv; s.off += m * sp.offset;
        if (sp.gas) moles += m / sp.molar_mass; else s.Nl += m / sp.rho0;
    }
    s.nR = moles * R;
    s.kind = s.M == 0 ? empty : (s.nR > 0 && s.Nl > 0 ? mixed_cell : (s.nR > 0 ? gas_cell : liquid_cell));
    return s;
}
LiquidAt liquid_at(const Model& model, const Fields& f, std::size_t N, std::size_t i, double p) {
    LiquidAt r;
    for (const auto k : model.liquids) {
        const double m = f.m[k * N + i]; if (m == 0) continue;
        const auto& sp = model.species[k];
        const double nominal = m / sp.rho0, q = p / sp.bulk;
        r.contracted += nominal * q / (1 + q);
        r.energy += nominal * sp.bulk * elastic_shape(q);
        r.compliance += nominal / sp.bulk / ((1 + q) * (1 + q));
        r.volume += nominal / (1 + q);
    }
    return r;
}
namespace {
double maximum_pressure(const Model& model, const Fields& f, std::size_t N, std::size_t i) {
    double result = std::numeric_limits<double>::max();
    for (const auto k : model.liquids) if (f.m[k * N + i] != 0)
        result = std::min(result, model.species[k].bulk * (model.species[k].maximum_ratio - 1));
    return result;
}
bool single_liquid(const Model& model, const Fields& f, std::size_t N, std::size_t i, std::size_t& which) {
    std::size_t count = 0;
    for (const auto k : model.liquids) if (f.m[k * N + i] != 0) { which = k; ++count; }
    return count == 1;
}
}
bool evaluate_state(const Model& model, const Fields& f, std::size_t N, std::size_t i, const Stats& s, double U,
    double V, State& out) {
    if (s.kind == empty) { out = {}; return U == 0; }
    const double A = U - s.off;
    if (!(A > 0) || !(V > 0)) return false;
    State r;
    if (s.kind == gas_cell) {
        r.T = A / s.Cv; r.p = s.nR * r.T / V; r.Vg = V; r.Eel = 0; r.Ceq = s.Cv;
        r.comp = s.Cv / (s.Cv + s.nR) * V / r.p;
    } else if (s.kind == liquid_cell) {
        const double delta = s.Nl - V; double p = 0;
        std::size_t which = 0;
        if (delta <= 0) {
            if (-delta > 32 * eps * V) return false; // internal void
        } else if (single_liquid(model, f, N, i, which)) {
            p = model.species[which].bulk * (delta / V);
        } else {
            double lo = 0, hi = maximum_pressure(model, f, N, i);
            p = std::min(hi, delta / std::max(liquid_at(model, f, N, i, 0).compliance, 1e-300));
            for (int it = 0; it < 200; ++it) {
                const auto l = liquid_at(model, f, N, i, p);
                const double res = l.contracted - delta;
                if (res < 0) lo = p; else hi = p;
                double next = p - res / l.compliance;
                if (!(next > lo && next < hi)) next = lo + (hi - lo) / 2;
                if (std::abs(next - p) <= 4 * eps * p || next == p) { p = next; break; }
                p = next;
            }
        }
        const double qmax = maximum_pressure(model, f, N, i);
        if (p - qmax > boundary(p, qmax)) return false;
        const auto l = liquid_at(model, f, N, i, p);
        r.p = p; r.Eel = l.energy; r.T = (A - l.energy) / s.Cv; r.Vg = 0;
        r.liquid_comp = l.compliance; r.comp = l.compliance; r.Ceq = s.Cv;
    } else {
        // p (V - Nl + C(p)) = nR (A - E(p)) / Cv: safeguarded Newton.
        double lo = 0, hi = maximum_pressure(model, f, N, i);
        const double headspace = V - s.Nl;
        double p = headspace > 0 ? s.nR * (A / s.Cv) / headspace : std::min(hi, s.nR * (A / s.Cv) / (1e-6 * V));
        p = std::min(p, hi);
        bool converged = false;
        for (int it = 0; it < 200; ++it) {
            const auto l = liquid_at(model, f, N, i, p);
            const double gasv = headspace + l.contracted;
            const double g = p * gasv - s.nR * (A - l.energy) / s.Cv;
            if (g < 0) lo = p; else hi = p;
            const double dg = gasv + p * l.compliance + s.nR * p * l.compliance / s.Cv;
            double next = p - g / dg;
            if (!(next > lo && next < hi)) next = lo + (hi - lo) / 2;
            if (std::abs(next - p) <= 8 * eps * p) { p = next; converged = true; break; }
            p = next;
        }
        if (!converged || !(p > 0)) return false;
        const auto l = liquid_at(model, f, N, i, p);
        r.p = p; r.Eel = l.energy; r.T = (A - l.energy) / s.Cv;
        if (!(r.T > 0)) return false;
        r.Vg = s.nR * r.T / p;
        if (!(r.Vg > 0)) return false;
        r.liquid_comp = l.compliance;
        r.comp = l.compliance + s.Cv / (s.Cv + s.nR) * r.Vg / p;
        r.Ceq = s.Cv + p * l.compliance * r.Vg / (r.T * (l.compliance + r.Vg / p));
    }
    if (!(r.T > 0) || !std::isfinite(r.T) || !std::isfinite(r.p) || !(r.comp > 0)) return false;
    // Port the interval kernel's conservative caloric/temperature admission.
    // A boundary uncertainty requests reference evaluation; it never enlarges
    // a material's physical domain or changes a stored state.
    if (A - r.Eel < 0x1p-30 * (std::abs(U) + std::abs(s.off) + std::abs(r.Eel))) throw EosRefinement{};
    for (std::size_t k = 0; k < model.K(); ++k) if (f.m[k * N + i] != 0) {
        const auto& sp = model.species[k];
        if (r.T - sp.tmin <= 64 * eps * std::max(r.T, sp.tmin) ||
            sp.tmax - r.T <= 64 * eps * std::max(r.T, sp.tmax)) throw EosRefinement{};
    }
    out = r;
    return true;
}
bool isentropic_work(const Model& model, const Fields& f, std::size_t N, std::size_t i, const Anchor& a, double dV, Work& out) {
    Work w;
    if (dV == 0) {
        w.dW = 0; w.dU = 0; w.p1 = a.p0;
        w.derivative = -.5 / a.comp; out = w; return true;
    }
    if (a.kind == gas_cell) {
        const double x = dV / a.V0;
        if (!(x > -1) || !std::isfinite(x)) return false;
        const double k = a.k;
        if (std::abs(x) < .25) {
            // f(x) = (1 - (1+x)^-k)/(k x) = sum d_n x^n, d_0 = 1: the increment
            // f-1 and f' are summed without cancellation (ratio < (k+n)|x|/(n+1)).
            double d = 1, power = 1, fm1 = 0, fp = 0;
            for (int n = 1; n <= 80; ++n) {
                d *= -(k + n) / (n + 1);
                fp += n * d * power;
                power *= x;
                const double term = d * power;
                fm1 += term;
                if (std::abs(term) <= 1e-18 * std::abs(fm1) && n >= 4) break;
            }
            w.dW = a.p0 * fm1;
            w.derivative = a.p0 * fp / a.V0;
            w.p1 = 0; // endpoint pressure is not needed on the series branch
        } else {
            const double L = std::log1p(x), e = std::expm1(-k * L);
            const double fval = -e / (k * x);
            w.dW = a.p0 * (fval - 1);
            w.p1 = a.p0 * std::exp(-(k + 1) * L);
            w.derivative = (w.p1 - a.p0 * fval) / dV;
        }
        w.dU = -(a.p0 + w.dW) * dV;
    } else if (a.kind == liquid_cell && a.single_liquid) {
        const double V1 = a.V0 + dV;
        if (!(V1 > 0)) return false;
        const double s = -dV / V1, scale = a.K + a.p0;
        w.p1 = a.p0 + scale * s;
        if (w.p1 < 0 && -w.p1 > 32 * eps * scale) return false; // would open an internal void
        if (w.p1 / a.K - a.qmax > boundary(w.p1 / a.K, a.qmax)) return false;
        double g = 0, gp = 0;
        if (std::abs(s) < 1e-3) {
            double sigma = 0, slope = 0; shape_series(s, sigma, slope);
            g = (1 + s) * s * sigma; gp = s * sigma + (1 + s) * (sigma + s * slope);
        } else {
            const double sh = std::log1p(s) - s / (1 + s);
            g = (1 + s) * sh / s; gp = sh / s + 1 / (1 + s) - (1 + s) * sh / (s * s);
        }
        w.dW = scale * g;
        w.derivative = scale * gp * (-a.V0 / (V1 * V1));
        w.dU = -(a.p0 + w.dW) * dV;
    } else {
        // General anchored isentrope: parameter l (log pressure ratio) with
        // gas, or the pressure increment for multi-liquid cells.
        const bool gas = a.nR > 0;
        const double am = gas ? a.nR / (a.Cv + a.nR) : 0, bm = gas ? a.Cv / (a.Cv + a.nR) : 1;
        double param = gas ? -dV / (a.p0 * a.comp) : -dV / a.comp;
        const auto point = [&](double t, double& dp, double& p, double& volume, double& slope) {
            dp = gas ? a.p0 * std::expm1(t) : t; p = a.p0 + dp;
            const double dpdt = gas ? p : 1;
            volume = 0; slope = 0;
            if (gas) { volume += a.Vg0 * std::expm1(-bm * t); slope += -bm * a.Vg0 * std::exp(-bm * t); }
            for (const auto k : model.liquids) {
                const double m = f.m[k * N + i]; if (m == 0) continue;
                const auto& sp = model.species[k];
                const double v0 = m / sp.rho0 / (1 + a.p0 / sp.bulk);
                volume += -v0 * dp / (sp.bulk + p);
                slope += -v0 * dpdt * (sp.bulk + a.p0) / ((sp.bulk + p) * (sp.bulk + p));
            }
        };
        double dp = 0, p = a.p0;
        bool converged = false;
        for (int it = 0; it < 60; ++it) {
            double volume = 0, slope = 0; point(param, dp, p, volume, slope);
            if (!(p >= 0) || !std::isfinite(volume)) return false;
            const double res = volume - dV;
            if (std::abs(res) <= 4 * eps * (std::abs(dV) + 1e-300)) { converged = true; break; }
            const double next = param - res / slope;
            if (!std::isfinite(next)) return false;
            if (std::abs(next - param) <= 2 * eps * std::abs(param)) { param = next; point(param, dp, p, volume, slope); converged = true; break; }
            param = next;
        }
        if (!converged) return false;
        if (!(p >= 0)) return false;
        double energy = 0, derivative = 0;
        if (gas) {
            energy += a.Cv * a.T0 * std::expm1(am * param);
            const double ratio = param / dV;
            derivative += -a.nR * a.T0 * bm * ratio * ratio * gas_second_difference(param, am, bm);
        }
        for (const auto k : model.liquids) {
            const double m = f.m[k * N + i]; if (m == 0) continue;
            const auto& sp = model.species[k];
            const double nominal = m / sp.rho0, relative = dp / (sp.bulk + a.p0), fraction = dp / (sp.bulk + p);
            energy += nominal * sp.bulk * elastic_shape(relative) + nominal * sp.bulk * (a.p0 / (sp.bulk + a.p0)) * fraction;
            const double ratio = relative / dV;
            derivative += -nominal * sp.bulk * ratio * ratio * log_second_difference(relative);
            if (p / sp.bulk - (sp.maximum_ratio - 1) > boundary(p / sp.bulk, sp.maximum_ratio - 1)) return false;
        }
        w.dU = energy;
        w.derivative = derivative;
        w.p1 = p;
        w.dW = std::abs(dp) < std::abs(a.p0) / 8 ? dp - dV * derivative : -energy / dV - a.p0;
        if (!(derivative < 0) || !std::isfinite(w.dW)) return false;
    }
    if (!std::isfinite(w.dW) || !std::isfinite(w.derivative) || !(w.derivative < 0) || !std::isfinite(w.dU)) return false;
    out = w;
    return true;
}
}
