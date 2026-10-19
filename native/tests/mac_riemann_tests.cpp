#include "mac_fixture.hpp"
#include <array>
#include <iomanip>
#include <numeric>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Primitive { double rho, u, p; };
struct Riemann {
    Primitive left, right;
    double gamma, cl, cr, pressure, velocity, dl, dr, lhead, ltail, rtail, rhead;
    Riemann(Primitive l, Primitive r, double g) : left(l), right(r), gamma(g), cl(std::sqrt(g*l.p/l.rho)), cr(std::sqrt(g*r.p/r.rho)) {
        check(r.u-l.u < 2*(cl+cr)/(g-1), "Riemann reference would create exact vacuum");
        const auto curve = [&](const Primitive& s, double sound, double p) {
            return p>s.p ? (p-s.p)*std::sqrt(2/((g+1)*s.rho)/(p+(g-1)/(g+1)*s.p)) :
                2*sound/(g-1)*(std::pow(p/s.p,(g-1)/(2*g))-1);
        };
        const auto residual = [&](double p) { return curve(l,cl,p)+curve(r,cr,p)+r.u-l.u; };
        double lo=0, hi=std::max(l.p,r.p);
        for(int i=0;residual(hi)<0;++i) { check(i<128,"Riemann pressure bracket exhausted"); hi*=2; }
        for(int i=0;i<256;++i) { const double mid=std::midpoint(lo,hi); if(mid==lo||mid==hi) break; if(residual(mid)>0) hi=mid; else lo=mid; }
        pressure=std::midpoint(lo,hi); velocity=.5*(l.u+r.u+curve(r,cr,pressure)-curve(l,cl,pressure));
        const double beta=(g-1)/(g+1);
        dl=l.rho*(pressure>l.p ? (pressure/l.p+beta)/(beta*pressure/l.p+1) : std::pow(pressure/l.p,1/g));
        dr=r.rho*(pressure>r.p ? (pressure/r.p+beta)/(beta*pressure/r.p+1) : std::pow(pressure/r.p,1/g));
        lhead=pressure>l.p ? l.u-cl*std::sqrt((g+1)/(2*g)*pressure/l.p+(g-1)/(2*g)) : l.u-cl;
        ltail=pressure>l.p ? lhead : velocity-std::sqrt(g*pressure/dl);
        rhead=pressure>r.p ? r.u+cr*std::sqrt((g+1)/(2*g)*pressure/r.p+(g-1)/(2*g)) : r.u+cr;
        rtail=pressure>r.p ? rhead : velocity+std::sqrt(g*pressure/dr);
    }
    Primitive at(double xi) const {
        if(xi<=velocity) {
            if(xi<=lhead) return left;
            if(xi>=ltail) return {dl,velocity,pressure};
            const double c=2/(gamma+1)*(cl+.5*(gamma-1)*(left.u-xi)), f=c/cl;
            return {left.rho*std::pow(f,2/(gamma-1)),2/(gamma+1)*(cl+.5*(gamma-1)*left.u+xi),left.p*std::pow(f,2*gamma/(gamma-1))};
        }
        if(xi>=rhead) return right;
        if(xi<=rtail) return {dr,velocity,pressure};
        const double c=2/(gamma+1)*(cr+.5*(gamma-1)*(xi-right.u)), f=c/cr;
        return {right.rho*std::pow(f,2/(gamma-1)),2/(gamma+1)*(-cr+.5*(gamma-1)*right.u+xi),right.p*std::pow(f,2*gamma/(gamma-1))};
    }
    Primitive average(double a, double b, double time) const {
        std::vector<double> points{a,b};
        for(double speed:{lhead,ltail,velocity,rtail,rhead}) { const double x=.5+time*speed; if(x>a&&x<b) points.push_back(x); }
        std::sort(points.begin(),points.end());
        constexpr std::array<double,4> nodes{.18343464249564980494,.52553240991632898582,.79666647741362673959,.96028985649753623168};
        constexpr std::array<double,4> weights{.36268378337836198297,.31370664587788728734,.22238103445337447054,.10122853629037625915};
        Primitive value{0,0,0};
        for(std::size_t k=1;k<points.size();++k) {
            const double mid=std::midpoint(points[k-1],points[k]), half=.5*(points[k]-points[k-1]);
            for(std::size_t j=0;j<nodes.size();++j) for(double sign:{-1.,1.}) {
                const auto p=at((mid+sign*half*nodes[j]-.5)/time); const double w=half*weights[j]/(b-a);
                value.rho+=w*p.rho; value.u+=w*p.u; value.p+=w*p.p;
            }
        }
        return value;
    }
};
// Classical baseline for the separation core (user decision 2026-10-19): binary64
// primitive MUSCL-Hancock with minmod slopes, mirrored rigid-wall ghosts and
// exact-Riemann Godunov fluxes sampled at x/t=0, on one row of the same grid,
// timestep and duration. review_mac_riemann.py repeats it operation for operation.
Primitive reference_face(const Primitive& l, const Primitive& r, double g) {
    const double cl=std::sqrt(g*l.p/l.rho), cr=std::sqrt(g*r.p/r.rho);
    const auto fan=[&](const Primitive& s, double c, double side, double xi) -> Primitive {
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
    const auto curve=[&](const Primitive& s, double c, double p) {
        return p>s.p ? (p-s.p)*std::sqrt(2/((g+1)*s.rho)/(p+(g-1)/(g+1)*s.p)) : 2*c/(g-1)*(std::pow(p/s.p,(g-1)/(2*g))-1);
    };
    const auto residual=[&](double p) { return curve(l,cl,p)+curve(r,cr,p)+r.u-l.u; };
    double lo=0, hi=std::max(l.p,r.p);
    for(int i=0;residual(hi)<0;++i) { check(i<128,"Reference Riemann bracket exhausted"); hi*=2; }
    for(int i=0;i<200;++i) { const double mid=.5*(lo+hi); if(mid==lo||mid==hi) break; if(residual(mid)>0) hi=mid; else lo=mid; }
    const double p=.5*(lo+hi), u=.5*(l.u+r.u+curve(r,cr,p)-curve(l,cl,p)), beta=(g-1)/(g+1);
    if(0<=u) {
        if(p>l.p) return 0<=l.u-cl*std::sqrt((g+1)/(2*g)*p/l.p+(g-1)/(2*g)) ? l : Primitive{l.rho*(p/l.p+beta)/(beta*p/l.p+1),u,p};
        const double d=l.rho*std::pow(p/l.p,1/g);
        if(0<=l.u-cl) return l;
        if(0>=u-std::sqrt(g*p/d)) return {d,u,p};
        return fan(l,cl,1,0);
    }
    if(p>r.p) return 0>=r.u+cr*std::sqrt((g+1)/(2*g)*p/r.p+(g-1)/(2*g)) ? r : Primitive{r.rho*(p/r.p+beta)/(beta*p/r.p+1),u,p};
    const double d=r.rho*std::pow(p/r.p,1/g);
    if(0>=r.u+cr) return r;
    if(0<=u+std::sqrt(g*p/d)) return {d,u,p};
    return fan(r,cr,-1,0);
}
std::array<double,2> reference_central_errors(const Riemann& exact, double g, std::size_t n, double duration, double dt) {
    const double h=1/static_cast<double>(n);
    std::vector<std::array<double,3>> cells(n);
    for(std::size_t x=0;x<n;++x) {
        const auto& s=x<n/2 ? exact.left : exact.right; cells[x]={s.rho,s.rho*s.u,s.p/(g-1)+.5*s.rho*s.u*s.u};
    }
    const auto primitive=[&](const std::array<double,3>& c) {
        const double u=c[1]/c[0], p=(g-1)*(c[2]-.5*c[0]*u*u);
        check(c[0]>0&&p>0&&std::isfinite(p),"Separation reference left its physical domain");
        return Primitive{c[0],u,p};
    };
    const auto minmod=[](double a, double b) { return a*b<=0 ? 0. : std::abs(a)<std::abs(b) ? a : b; };
    std::vector<Primitive> w(n), lface(n), rface(n);
    std::vector<std::array<double,3>> flux(n+1);
    for(double t=0;t<duration;) {
        const double step=std::min(dt,duration-t);
        for(std::size_t x=0;x<n;++x) w[x]=primitive(cells[x]);
        for(std::size_t x=0;x<n;++x) {
            const Primitive wl=x>0 ? w[x-1] : Primitive{w[0].rho,-w[0].u,w[0].p};
            const Primitive wr=x+1<n ? w[x+1] : Primitive{w[n-1].rho,-w[n-1].u,w[n-1].p};
            const double d0=minmod(w[x].rho-wl.rho,wr.rho-w[x].rho), d1=minmod(w[x].u-wl.u,wr.u-w[x].u), d2=minmod(w[x].p-wl.p,wr.p-w[x].p);
            const double rho=w[x].rho, u=w[x].u, p=w[x].p;
            const double drho=-.5*step/h*(u*d0+rho*d1), du=-.5*step/h*(u*d1+d2/rho), dp=-.5*step/h*(g*p*d1+u*d2);
            lface[x]={rho-.5*d0+drho,u-.5*d1+du,p-.5*d2+dp}; rface[x]={rho+.5*d0+drho,u+.5*d1+du,p+.5*d2+dp};
        }
        for(std::size_t f=0;f<=n;++f) {
            Primitive s;
            if(f==0) s=reference_face({lface[0].rho,-lface[0].u,lface[0].p},lface[0],g);
            else if(f==n) s=reference_face(rface[n-1],{rface[n-1].rho,-rface[n-1].u,rface[n-1].p},g);
            else s=reference_face(rface[f-1],lface[f],g);
            if(s.rho>0) { const double e=s.p/(g-1)+.5*s.rho*s.u*s.u; flux[f]={s.rho*s.u,s.rho*s.u*s.u+s.p,s.u*(e+s.p)}; }
            else flux[f]={0,s.p,0};
        }
        for(std::size_t x=0;x<n;++x) for(int k=0;k<3;++k) cells[x][k]-=step/h*(flux[x+1][k]-flux[x][k]);
        t+=step;
    }
    std::array<double,2> central{0,0};
    for(std::size_t x:{n/2-1,n/2}) {
        const auto final=primitive(cells[x]); const auto expected=exact.average(static_cast<double>(x)*h,static_cast<double>(x+1)*h,duration);
        central[0]=std::max(central[0],std::abs(final.rho-expected.rho)/expected.rho);
        central[1]=std::max(central[1],std::abs(final.p-expected.p)/expected.p);
    }
    return central;
}
struct Errors { double rho=0, p=0, u=0; };
Errors run(int control,std::size_t n) {
    const auto& set=ma::testing::setup(); const auto ni=set.registry.index("nitrogen"), he=set.registry.index("helium");
    const auto& nitrogen=set.registry[ni]; const auto& helium=set.registry[he];
    const double rgas=gas_constant*(1/nitrogen.molar_mass+1e-28/helium.molar_mass)/(1+1e-28);
    const double cv=(nitrogen.cv()+1e-28*helium.cv())/(1+1e-28), gamma=1+rgas/cv;
    const double unit=std::sqrt(100000.), sound=std::sqrt(gamma)*unit, h=1/static_cast<double>(n), volume=h*h;
    const double mach=control==0 ? 0 : control==1 ? 1.5 : 4;
    const Primitive left{1,control==2 ? -mach*sound : mach*sound,100000};
    const Primitive right{control==0 ? .125 : 1,-left.u,control==0 ? 10000. : 100000.};
    const double duration=(control==0 ? .2 : control==1 ? .06 : .03)/unit;
    const double window_lo=control==0 ? 0 : .25, window_hi=control==0 ? 1 : .75;
    const Riemann exact(left,right,gamma);
    if(n==64) std::cout<<"RPROBLEM "<<control<<' '<<left.rho<<' '<<left.u<<' '<<left.p<<' '<<right.rho<<' '<<right.u<<' '<<right.p<<' '<<window_lo<<' '<<window_hi<<'\n';
    auto source=set.state(n,2,false); source.grid.periodic_y=true; source.grid.spacing_m=h;
    source.momentum_x.assign(2*(n+1),0); source.momentum_y.assign(2*n,0);
    for(std::size_t y=0;y<2;++y) for(std::size_t x=0;x<n;++x) {
        const auto& p=x<n/2 ? left : right; auto& cell=source.cells[x+y*n]; std::vector<double> masses(set.registry.size());
        masses[ni]=p.rho*volume; masses[he]=masses[ni]*1e-28;
        cell.mixture=set.eos->initialize_at_temperature(masses,p.p/(p.rho*rgas),volume).mixture;
        cell.passive_quantities={{17,cell.mixture.mass()},{UINT64_C(0x80000000),cell.mixture.mass()*1e-30}};
    }
    const auto initial_inertia=mac_face_inertias(set.registry,source,*set.eos);
    for(std::size_t y=0;y<2;++y) for(std::size_t x=1;x<n;++x) source.momentum_x[source.grid.x_face(x,y)]=
        initial_inertia.x[source.grid.x_face(x,y)]*(x==n/2 ? .5*(left.u+right.u) : x<n/2 ? left.u : right.u);
    const auto held=ma::testing::fingerprint(source); MacClosedSettings settings;
    settings.maximum_timestep_s=.25*h/((control==0 ? 2 : 1+mach)*sound);
    const MacClosedWorld world(set.registry,set.eos,source,settings); const auto result=world.advanced(duration,16384);
    const auto& final=result.world.state(); const auto final_inertia=mac_face_inertias(set.registry,final,*set.eos);
    Errors error; long double entropy=0, entropy_bound=0; double centre_rho=0, centre_p=0;
    for(int endpoint=0;endpoint<2;++endpoint) {
        const auto& state=endpoint==0 ? source : final;
        for(std::size_t i=0;i<state.cells.size();++i) {
            const auto& c=state.cells[i]; const auto eos=set.eos->evaluate(c.mixture,volume); const auto x=i%n;
            std::cout<<"RSTATE "<<control<<' '<<n<<' '<<endpoint<<' '<<i<<' '<<c.mixture.masses[ni]<<' '<<c.mixture.masses[he]<<' '<<c.mixture.energy<<' '<<
                eos.mechanical_pressure_pa<<' '<<eos.temperature_k<<' '<<eos.entropy_j_per_k<<' '<<c.passive_quantities[0].quantity<<' '<<c.passive_quantities[1].quantity<<'\n';
            entropy+=(endpoint==0 ? -1.L : 1.L)*eos.entropy_j_per_k;
            entropy_bound+=128*std::numeric_limits<double>::epsilon()*std::abs(c.mixture.energy)/eos.temperature_k;
            check(c.mixture.masses[ni]>0&&c.mixture.masses[he]>0&&c.passive_quantities[1].quantity>0,"Strong flow lost positive inventory/trace");
            if(endpoint==1) {
                const double a=static_cast<double>(x)*h,b=a+h; const auto expected=exact.average(a,b,duration);
                if(a>=window_lo&&b<=window_hi) {
                    error.rho+=.5*h*std::abs(c.mixture.mass()/volume-expected.rho)/(window_hi-window_lo);
                    error.p+=.5*h*std::abs(eos.mechanical_pressure_pa-expected.p)/(100000*(window_hi-window_lo));
                }
                if(x==n/2||x+1==n/2) {
                    centre_rho=std::max(centre_rho,std::abs(c.mixture.mass()/volume-expected.rho)/expected.rho);
                    centre_p=std::max(centre_p,std::abs(eos.mechanical_pressure_pa-expected.p)/expected.p);
                }
            }
        }
    }
    for(int axis=0;axis<2;++axis) {
        const auto& j0=axis==0 ? source.momentum_x : source.momentum_y; const auto& j1=axis==0 ? final.momentum_x : final.momentum_y;
        for(std::size_t f=0;f<j0.size();++f) std::cout<<"RFACE "<<control<<' '<<n<<' '<<axis<<' '<<f<<' '<<j0[f]<<' '<<j1[f]<<'\n';
    }
    for(std::size_t y=0;y<2;++y) for(std::size_t x=0;x<n;++x) {
        const double position=static_cast<double>(x)*h;
        if(position>=window_lo&&position<window_hi) error.u+=.5*h*std::abs(final.momentum_x[final.grid.x_face(x,y)]/
            final_inertia.x[final.grid.x_face(x,y)]-exact.at((position-.5)/duration).u)/(sound*(window_hi-window_lo));
    }
    const auto& report=result.report;
    std::cout<<"RMODE "<<control<<' '<<n<<' '<<h<<' '<<duration<<' '<<settings.maximum_timestep_s<<' '<<report.substeps<<' '<<report.rejected_attempts<<' '<<
        report.energy_residual_j<<' '<<report.energy_bound_j<<' '<<static_cast<double>(entropy)<<' '<<static_cast<double>(entropy_bound)<<' '<<
        error.rho<<' '<<error.p<<' '<<error.u<<' '<<centre_rho<<' '<<centre_p<<' '<<report.wall_impulse_x<<' '<<report.wall_impulse_y<<'\n';
    check(ma::testing::fingerprint(source)==held&&world.time_s()==0&&result.world.time_s()==duration,"Strong-flow advance changed source or clock");
    check(report.rejected_attempts==0&&report.substeps<=static_cast<std::size_t>(std::ceil(duration/settings.maximum_timestep_s))+2,"Strong-flow nominal timestep path changed");
    check(entropy>=-entropy_bound,"Strong flow reduced total thermodynamic entropy beyond stored-U allowance");
    if(n==256) check(std::max({error.rho,error.p,error.u})<.06,"Finest strong-flow error exceeds .06");
    if(control==2&&n==256) {
        // Central density stays within half its exact cell average. Central
        // pressure may not exceed the classical MUSCL-Hancock reference error.
        const auto reference=reference_central_errors(exact,gamma,n,duration,settings.maximum_timestep_s);
        std::cout<<"RREFERENCE "<<control<<' '<<n<<' '<<reference[0]<<' '<<reference[1]<<'\n';
        check(centre_rho<.5&&centre_p<=reference[1],"Fine near-vacuum central gas exceeds its density or classical pressure bound");
    }
    return error;
}
void refinement(int control) {
    Errors previous;
    for(const auto n:{std::size_t(64),std::size_t(128),std::size_t(256)}) {
        const auto current=run(control,n);
        if(n!=64) check(current.rho<.85*previous.rho&&current.p<.85*previous.p&&current.u<.85*previous.u,"Strong-flow joint rho/p/u refinement exceeds .85");
        previous=current;
    }
}
}
int main() {
    try {
        std::cout<<std::setprecision(17);
        for(const auto& m:ma::testing::setup().eos->reference().materials()) std::cout<<"MATERIAL "<<ma::testing::setup().registry.index(m.id)<<' '<<
            (m.phase==ReferencePhase::ideal_gas ? 0 : 1)<<' '<<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<' '<<m.energy_offset_j_per_kg<<' '<<
            m.zero_pressure_density_kg_per_m3<<' '<<m.reference_bulk_modulus_pa<<'\n';
        std::cout<<"RIDS "<<ma::testing::setup().registry.index("nitrogen")<<' '<<ma::testing::setup().registry.index("helium")<<'\n';
        refinement(0); std::cout<<"PASS Sod shock/contact/rarefaction refinement\n";
        refinement(1); std::cout<<"PASS opposed Mach-1.5 gas collision refinement\n";
        refinement(2); std::cout<<"PASS positive Mach-4 separating rarefaction refinement\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; }
}
