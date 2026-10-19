#pragma once
// Binary64 primitive MUSCL-Hancock reference for the strong-flow gate
// (MAC_STRONG_FLOW.md, acceptance criteria): minmod slopes in density, velocity
// and pressure, mirrored rigid-wall ghost cells, a half-step primitive predictor
// and Godunov fluxes from the exact Riemann state at x/t = 0 (bisection for the
// star pressure, two-rarefaction vacuum state when ur - ul >= 2(cl + cr)/(g - 1)).
// review_mac_riemann.py repeats every function here operation for operation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ma::testing::riemann_reference {
struct State { double rho, u, p; };
using Cell = std::array<double, 3>;
inline void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
inline State face(const State& l, const State& r, double g) {
    const double cl=std::sqrt(g*l.p/l.rho), cr=std::sqrt(g*r.p/r.rho);
    const auto fan=[&](const State& s, double c, double side, double xi) -> State {
        const double f=2/(g+1)*(c+side*.5*(g-1)*(s.u-xi))/c;
        return {s.rho*std::pow(f,2/(g-1)),2/(g+1)*(side*c+.5*(g-1)*s.u+xi),s.p*std::pow(f,2*g/(g-1))};
    };
    if(r.u-l.u>=2*(cl+cr)/(g-1)) {
        if(0<=l.u-cl) return l;
        if(0<l.u+2*cl/(g-1)) return fan(l,cl,1,0);
        if(0<=r.u-2*cr/(g-1)) return {0,0,0};
        if(0>=r.u+cr) return r;
        return fan(r,cr,-1,0);
    }
    const auto curve=[&](const State& s, double c, double p) {
        return p>s.p ? (p-s.p)*std::sqrt(2/((g+1)*s.rho)/(p+(g-1)/(g+1)*s.p)) : 2*c/(g-1)*(std::pow(p/s.p,(g-1)/(2*g))-1);
    };
    const auto residual=[&](double p) { return curve(l,cl,p)+curve(r,cr,p)+r.u-l.u; };
    double lo=0, hi=std::max(l.p,r.p);
    for(int i=0;residual(hi)<0;++i) { require(i<128,"Reference Riemann bracket exhausted"); hi*=2; }
    for(int i=0;i<200;++i) { const double mid=.5*(lo+hi); if(mid==lo||mid==hi) break; if(residual(mid)>0) hi=mid; else lo=mid; }
    const double p=.5*(lo+hi), u=.5*(l.u+r.u+curve(r,cr,p)-curve(l,cl,p)), beta=(g-1)/(g+1);
    if(0<=u) {
        if(p>l.p) return 0<=l.u-cl*std::sqrt((g+1)/(2*g)*p/l.p+(g-1)/(2*g)) ? l : State{l.rho*(p/l.p+beta)/(beta*p/l.p+1),u,p};
        const double d=l.rho*std::pow(p/l.p,1/g);
        if(0<=l.u-cl) return l;
        if(0>=u-std::sqrt(g*p/d)) return {d,u,p};
        return fan(l,cl,1,0);
    }
    if(p>r.p) return 0>=r.u+cr*std::sqrt((g+1)/(2*g)*p/r.p+(g-1)/(2*g)) ? r : State{r.rho*(p/r.p+beta)/(beta*p/r.p+1),u,p};
    const double d=r.rho*std::pow(p/r.p,1/g);
    if(0>=r.u+cr) return r;
    if(0<=u+std::sqrt(g*p/d)) return {d,u,p};
    return fan(r,cr,-1,0);
}
inline State primitive(const Cell& c, double g) {
    const double u=c[1]/c[0], p=(g-1)*(c[2]-.5*c[0]*u*u);
    require(c[0]>0&&p>0&&std::isfinite(p),"Separation reference left its physical domain");
    return {c[0],u,p};
}
inline Cell conserved(const State& s, double g) { return {s.rho,s.rho*s.u,s.p/(g-1)+.5*s.rho*s.u*s.u}; }
// Advance a closed row of cells (rigid mirrored walls at both ends) to `duration`
// with steps of at most `dt`, ending on a partial step.
inline void advance(std::vector<Cell>& cells, double g, double h, double duration, double dt) {
    const std::size_t n=cells.size();
    const auto minmod=[](double a, double b) { return a*b<=0 ? 0. : std::abs(a)<std::abs(b) ? a : b; };
    std::vector<State> w(n), lface(n), rface(n);
    std::vector<Cell> flux(n+1);
    for(double t=0;t<duration;) {
        const double step=std::min(dt,duration-t);
        for(std::size_t x=0;x<n;++x) w[x]=primitive(cells[x],g);
        for(std::size_t x=0;x<n;++x) {
            const State wl=x>0 ? w[x-1] : State{w[0].rho,-w[0].u,w[0].p};
            const State wr=x+1<n ? w[x+1] : State{w[n-1].rho,-w[n-1].u,w[n-1].p};
            const double d0=minmod(w[x].rho-wl.rho,wr.rho-w[x].rho), d1=minmod(w[x].u-wl.u,wr.u-w[x].u), d2=minmod(w[x].p-wl.p,wr.p-w[x].p);
            const double rho=w[x].rho, u=w[x].u, p=w[x].p;
            const double drho=-.5*step/h*(u*d0+rho*d1), du=-.5*step/h*(u*d1+d2/rho), dp=-.5*step/h*(g*p*d1+u*d2);
            lface[x]={rho-.5*d0+drho,u-.5*d1+du,p-.5*d2+dp}; rface[x]={rho+.5*d0+drho,u+.5*d1+du,p+.5*d2+dp};
        }
        for(std::size_t f=0;f<=n;++f) {
            State s;
            if(f==0) s=face({lface[0].rho,-lface[0].u,lface[0].p},lface[0],g);
            else if(f==n) s=face(rface[n-1],{rface[n-1].rho,-rface[n-1].u,rface[n-1].p},g);
            else s=face(rface[f-1],lface[f],g);
            if(s.rho>0) { const double e=s.p/(g-1)+.5*s.rho*s.u*s.u; flux[f]={s.rho*s.u,s.rho*s.u*s.u+s.p,s.u*(e+s.p)}; }
            else flux[f]={0,s.p,0};
        }
        for(std::size_t x=0;x<n;++x) for(int k=0;k<3;++k) cells[x][static_cast<std::size_t>(k)]-=step/h*(flux[x+1][static_cast<std::size_t>(k)]-flux[x][static_cast<std::size_t>(k)]);
        t+=step;
    }
}
// Resting-slab reference of the gate's discrete start (user decision 2026-10-19):
// `refine` fine cells per gate cell (refine even), all at the left/right state
// except the `refine` fine cells of the centre face's dual [1/2 - 1/(2N), 1/2 + 1/(2N)],
// which are at rest. Fine timestep gate_dt / refine, the gate's duration. On the
// gate grid: density and pressure are the means of the fine-cell values, and face
// velocity is the momentum over mass of the two fine cells beside each gate face
// (zero at the rigid ends).
struct SlabProfile { std::vector<double> rho, p, u; };
inline SlabProfile resting_slab(const State& left, const State& right, double g, std::size_t gate, std::size_t refine,
    double duration, double gate_dt) {
    require(gate>=2&&gate%2==0&&refine>=2&&refine%2==0,"Invalid resting-slab reference grid");
    const std::size_t n=gate*refine; const double h=1/static_cast<double>(n);
    std::vector<Cell> cells(n);
    for(std::size_t x=0;x<n;++x) {
        const bool rest=x>=n/2-refine/2&&x<n/2+refine/2; const auto& s=x<n/2 ? left : right;
        cells[x]=conserved({s.rho,rest ? 0. : s.u,s.p},g);
    }
    advance(cells,g,h,duration,gate_dt/static_cast<double>(refine));
    SlabProfile profile{std::vector<double>(gate),std::vector<double>(gate),std::vector<double>(gate+1)};
    for(std::size_t i=0;i<gate;++i) {
        double rho=0, p=0;
        for(std::size_t k=0;k<refine;++k) { const auto s=primitive(cells[i*refine+k],g); rho+=s.rho; p+=s.p; }
        profile.rho[i]=rho/static_cast<double>(refine); profile.p[i]=p/static_cast<double>(refine);
    }
    for(std::size_t f=1;f<gate;++f) {
        const auto& a=cells[f*refine-1]; const auto& b=cells[f*refine];
        profile.u[f]=(a[1]+b[1])/(a[0]+b[0]);
    }
    return profile;
}
}
