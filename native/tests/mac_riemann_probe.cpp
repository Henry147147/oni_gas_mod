// Diagnostic copy of mac_riemann_tests.cpp (same inputs, grids and timesteps).
// It never stops on a gate criterion and is not a gate run.
// Usage: ma_mac_riemann_probe control n [mat=1 mom=1 comp=2 heat=2 profile=0 chunks=0 shift=0]
//   control 0 Sod, 1 collision, 2 separation; mat/mom/comp/heat are the owner selectors.
//   profile=1 prints row-0 cells: x, centre, rho, exact rho, p, exact p, T, face u, exact u.
//   chunks=K advances K equal sub-intervals (this changes the final partial steps) and
//   prints, for collision, the right-half window density error S in density*cells
//   (SK lines: shock region from the third cell, centre = first two cells); for the
//   other controls the central cells/faces (CTIME) and central masses (CMASS).
//   slab=R (separation) also compares with the resting-slab reference at refinement R
//   (SLAB line; with profile=1 also SLABPROFILE rows: x, rho, ref, p, ref, u, ref).
//   shift=1 (diagnostic only, not the gate input) puts the discontinuity at the
//   centre of cell n/2, so no face starts at rest; errors use the shifted exact
//   solution and the single straddling cell is the central cell.
// The last line is RESULT ... L1 rho p u central rho p (central only meaningful for 2).
#include "mac_fixture.hpp"
#include "mac_riemann_reference.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <numeric>
#include <string>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Primitive { double rho, u, p; };
struct Riemann {
    Primitive left, right;
    double gamma, cl, cr, pressure, velocity, dl, dr, lhead, ltail, rtail, rhead;
    Riemann(Primitive l, Primitive r, double g) : left(l), right(r), gamma(g), cl(std::sqrt(g*l.p/l.rho)), cr(std::sqrt(g*r.p/r.rho)) {
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
struct Options { int mat=1, mom=1, comp=2, heat=2, profile=0, chunks=0, shift=0, slab=0; };
struct Errors { double rho=0, p=0, u=0, crho=0, cp=0; };
Errors run(int control,std::size_t n,const Options& o) {
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
        initial_inertia.x[source.grid.x_face(x,y)]*(o.shift ? (x<=n/2 ? left.u : right.u) : x==n/2 ? .5*(left.u+right.u) : x<n/2 ? left.u : right.u);
    MacClosedSettings settings;
    settings.reconstruction=static_cast<MacMaterialReconstruction>(o.mat);
    settings.momentum_reconstruction=static_cast<MacMomentumReconstruction>(o.mom);
    settings.compression_viscosity=static_cast<MacCompressionViscosity>(o.comp);
    settings.shock_heat=static_cast<MacShockHeatRegularization>(o.heat);
    settings.maximum_timestep_s=.25*h/((control==0 ? 2 : 1+mach)*sound);
    MacClosedWorld world(set.registry,set.eos,source,settings);
    MacClosedAdvanceReport report;
    const auto central_line=[&](const MacClosedWorld& w,double t) {
        const auto& s=w.state(); const auto in=mac_face_inertias(set.registry,s,*set.eos);
        std::cout<<"CTIME "<<t;
        for(std::size_t x:{n/2-2,n/2-1}) {
            const auto& c=s.cells[x].mixture; const auto e=set.eos->evaluate(c,volume);
            const auto ex=exact.average(static_cast<double>(x)*h,static_cast<double>(x+1)*h,t);
            std::cout<<" | x"<<x<<" rho "<<c.mass()/volume<<" ex "<<ex.rho<<" p "<<e.mechanical_pressure_pa<<" ex "<<ex.p<<" T "<<e.temperature_k;
        }
        for(std::size_t x:{n/2-2,n/2-1,n/2}) { const auto f=s.grid.x_face(x,0); std::cout<<" | f"<<x<<" u "<<s.momentum_x[f]/in.x[f]<<" ex "<<exact.at((static_cast<double>(x)*h-.5)/t).u; }
        std::cout<<'\n'<<"CMASS "<<t;
        for(std::size_t k:{std::size_t(1),std::size_t(2),std::size_t(4),std::size_t(8),std::size_t(16)}) {
            double m=0; for(std::size_t x=n/2-k;x<n/2;++x) m+=s.cells[x].mixture.mass()/volume*h;
            const auto ex=exact.average(.5-static_cast<double>(k)*h,.5,t);
            std::cout<<" | "<<k<<" "<<m<<" ex "<<ex.rho*static_cast<double>(k)*h;
        }
        std::cout<<'\n';
    };
    if(o.chunks>0) {
        for(int k=0;k<o.chunks;++k) {
            auto r=world.advanced(duration/o.chunks,16384); world=r.world; report.substeps+=r.report.substeps; report.rejected_attempts+=r.report.rejected_attempts;
            if(control==1) {
                const double tt=duration*(k+1)/o.chunks; double S=0,Sc=0; const auto& st=world.state();
                for(std::size_t x=n/2;x<3*n/4;++x) { const double a=static_cast<double>(x)*h; const auto ex=exact.average(a,a+h,tt);
                    const double e=std::abs(st.cells[x].mixture.mass()/volume-ex.rho); if(x>=n/2+2) S+=e; else Sc+=e; }
                std::cout<<"SK t "<<tt<<" steps "<<(tt/settings.maximum_timestep_s)<<" shockcells "<<(exact.rhead*tt/h)<<" S "<<S<<" centre "<<Sc<<'\n';
            } else central_line(world,duration*(k+1)/o.chunks);
        }
    } else { auto r=world.advanced(duration,16384); report=r.report; world=r.world; }
    const auto& final=world.state(); const auto final_inertia=mac_face_inertias(set.registry,final,*set.eos);
    Errors error; long double entropy=0;
    for(int endpoint=0;endpoint<2;++endpoint) {
        const auto& state=endpoint==0 ? source : final;
        for(std::size_t i=0;i<state.cells.size();++i) {
            const auto& c=state.cells[i]; const auto eos=set.eos->evaluate(c.mixture,volume); const auto x=i%n;
            entropy+=(endpoint==0 ? -1.L : 1.L)*eos.entropy_j_per_k;
            if(endpoint==1) {
                const double a=static_cast<double>(x)*h,b=a+h; const double off=o.shift ? .5*h : 0; const auto expected=exact.average(a-off,b-off,duration);
                if(a>=window_lo&&b<=window_hi) {
                    error.rho+=.5*h*std::abs(c.mixture.mass()/volume-expected.rho)/(window_hi-window_lo);
                    error.p+=.5*h*std::abs(eos.mechanical_pressure_pa-expected.p)/(100000*(window_hi-window_lo));
                }
                if(o.shift ? x==n/2 : x==n/2||x+1==n/2) {
                    error.crho=std::max(error.crho,std::abs(c.mixture.mass()/volume-expected.rho)/expected.rho);
                    error.cp=std::max(error.cp,std::abs(eos.mechanical_pressure_pa-expected.p)/expected.p);
                }
                if(o.profile && i<n) {
                    const auto f=final.grid.x_face(x,0);
                    std::cout<<"PROFILE "<<x<<' '<<(a+.5*h)<<' '<<c.mixture.mass()/volume<<' '<<expected.rho<<' '<<eos.mechanical_pressure_pa<<' '<<expected.p<<' '<<eos.temperature_k
                        <<' '<<final.momentum_x[f]/final_inertia.x[f]<<' '<<exact.at((a-off-.5)/duration).u<<'\n';
                }
            }
        }
    }
    for(std::size_t y=0;y<2;++y) for(std::size_t x=0;x<n;++x) {
        const double position=static_cast<double>(x)*h;
        if(position>=window_lo&&position<window_hi) error.u+=.5*h*std::abs(final.momentum_x[final.grid.x_face(x,y)]/
            final_inertia.x[final.grid.x_face(x,y)]-exact.at((position-(o.shift ? .5*h : 0)-.5)/duration).u)/(sound*(window_hi-window_lo));
    }
    if(control==2&&o.slab>0&&!o.shift) {
        const auto slab=ma::testing::riemann_reference::resting_slab({left.rho,left.u,left.p},{right.rho,right.u,right.p},gamma,n,
            static_cast<std::size_t>(o.slab),duration,settings.maximum_timestep_s);
        double er=0,ep=0,eu=0,cr=0,cp=0; std::array<double,4> eu_region{0,0,0,0};
        for(std::size_t i=0;i<final.cells.size();++i) {
            const auto x=i%n; const double a=static_cast<double>(x)*h,b=a+h;
            const double rho=final.cells[i].mixture.mass()/volume, pp=set.eos->evaluate(final.cells[i].mixture,volume).mechanical_pressure_pa;
            if(a>=window_lo&&b<=window_hi) { er+=.5*h*std::abs(rho-slab.rho[x])/(window_hi-window_lo); ep+=.5*h*std::abs(pp-slab.p[x])/(100000*(window_hi-window_lo)); }
            if(x==n/2||x+1==n/2) { cr=std::max(cr,std::abs(rho-slab.rho[x])/slab.rho[x]); cp=std::max(cp,std::abs(pp-slab.p[x])/slab.p[x]); }
            if(o.profile&&i<n) std::cout<<"SLABPROFILE "<<x<<' '<<rho<<' '<<slab.rho[x]<<' '<<pp<<' '<<slab.p[x]<<' '<<final.momentum_x[final.grid.x_face(x,0)]/final_inertia.x[final.grid.x_face(x,0)]<<' '<<slab.u[x]<<'\n';
        }
        for(std::size_t y=0;y<2;++y) for(std::size_t x=0;x<n;++x) {
            const double position=static_cast<double>(x)*h;
            if(position>=window_lo&&position<window_hi) {
                const double e=.5*h*std::abs(final.momentum_x[final.grid.x_face(x,y)]/final_inertia.x[final.grid.x_face(x,y)]-slab.u[x])/(sound*(window_hi-window_lo));
                eu+=e; const double d=std::abs(position-.5); eu_region[d<.05 ? 0 : d<.1 ? 1 : d<.18 ? 2 : 3]+=e;
            }
        }
        std::cout<<"SLAB refine "<<o.slab<<" L1 "<<er<<' '<<ep<<' '<<eu<<" central "<<cr<<' '<<cp<<" u by |x-.5| <.05/<.1/<.18/rest "<<eu_region[0]<<' '<<eu_region[1]<<' '<<eu_region[2]<<' '<<eu_region[3]<<'\n';
    }
    std::cout<<"RESULT control "<<control<<" n "<<n<<" steps "<<report.substeps<<" rejected "<<report.rejected_attempts<<" limit "<<
        static_cast<std::size_t>(std::ceil(duration/settings.maximum_timestep_s))+2<<" entropy "<<static_cast<double>(entropy)<<
        " L1 "<<error.rho<<' '<<error.p<<' '<<error.u<<" central "<<error.crho<<' '<<error.cp<<'\n';
    return error;
}
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::setprecision(10);
        check(argc>=3,"usage: control n [key=value]");
        const int control=std::atoi(argv[1]); const auto n=static_cast<std::size_t>(std::atoi(argv[2]));
        Options o;
        for(int k=3;k<argc;++k) {
            const std::string a=argv[k]; const auto eq=a.find('='); check(eq!=std::string::npos,"key=value");
            const auto key=a.substr(0,eq); const int value=std::atoi(a.c_str()+eq+1);
            if(key=="mat") o.mat=value; else if(key=="mom") o.mom=value; else if(key=="comp") o.comp=value; else if(key=="heat") o.heat=value;
            else if(key=="profile") o.profile=value; else if(key=="chunks") o.chunks=value; else if(key=="shift") o.shift=value; else if(key=="slab") o.slab=value; else check(false,"unknown key");
        }
        run(control,n,o);
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; }
}
