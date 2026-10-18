#include "mixed_atmospheres/mac_energy.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void encoded(MacEnergy e) { std::cout<<e.mantissa()<<' '<<e.exponent(); }
void roundtrips() {
    int id=0;
    for(double v:{0.,1.,-1.,std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min(),-std::numeric_limits<double>::min(),std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),1e-290,-1e300}) {
        const auto e=MacEnergy::from_double(v);const auto p=e.project();
        check(p.value==v&&!p.rounded&&e.require_representable()==v,"MAC energy roundtrip changed a finite binary64 value");
        std::cout<<"ROUND "<<id++<<' '<<v<<' ';encoded(e);std::cout<<' '<<p.value<<' '<<p.rounded<<'\n';
    }
    // Compare the optimized normalization/projection with the C math oracle
    // over every exponent bin and both signs, including random subnormals.
    std::uint64_t seed=UINT64_C(0xb3f47519ad72860e);
    for(std::uint64_t power=0;power<2047;++power)for(int sample=0;sample<8;++sample) {
        seed=seed*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
        const auto bits=(seed&UINT64_C(0x800fffffffffffff))|(power<<52);
        const double value=std::bit_cast<double>(bits); int expected_power=0;
        const double expected_mantissa=std::frexp(value,&expected_power);
        const auto actual=MacEnergy::from_double(value); const auto projected=actual.project();
        check(actual.mantissa()==expected_mantissa && actual.exponent()==expected_power &&
            std::bit_cast<std::uint64_t>(projected.value)==bits && !projected.rounded,
            "Common-case energy arithmetic differs from frexp or loses projection bits");
    }
}
void products() {
    int id=0;
    for(const auto& pair:{std::pair{1.,2.},std::pair{3.0517578125e-5,2.1665e-147},std::pair{1e-300,1e-300},
        std::pair{1e300,1e300},std::pair{1.,std::numeric_limits<double>::denorm_min()}}) {
        const auto k=MacEnergy::kinetic(pair.first,pair.second);check(k.mantissa()>0,"Positive derived K disappeared");
        std::cout<<"KIN "<<id++<<' '<<pair.first<<' '<<pair.second<<' ';encoded(k);std::cout<<'\n';
    }
    id=0;
    for(const auto& pair:{std::pair{1.,2.},std::pair{1e-300,1e-300},std::pair{1e300,-1e300},std::pair{1.,std::numeric_limits<double>::denorm_min()}}) {
        const auto k=MacEnergy::variance(pair.first,pair.second);check(k.mantissa()>0,"Positive derived variance disappeared");
        std::cout<<"VAR "<<id++<<' '<<pair.first<<' '<<pair.second<<' ';encoded(k);std::cout<<'\n';
    }
    const auto source=MacEnergy::from_double(7.6902724379882565e-290);
    const auto parcel=source.shared(3.6197301863874292e-151,3.0517578125e-5);
    check(parcel.mantissa()>0&&parcel.project().value==0&&parcel.project().rounded,"Original derived-energy underflow was not retained/reported");
    std::cout<<"SHARE 0 ";encoded(source);std::cout<<" 3.6197301863874292e-151 3.0517578125e-5 ";encoded(parcel);std::cout<<'\n';
    for(int i=1;i<=4;++i) {
        const auto input=MacEnergy::from_parts(i%2 ? -.75 : .625,i%2 ? -4096 : 4096);
        const double part=i<3 ? 1e-300 : 1e300,total=i<3 ? 1e300 : 1e-300;
        const auto output=input.shared(part,total);
        std::cout<<"SHARE "<<i<<' ';encoded(input);std::cout<<' '<<part<<' '<<total<<' ';encoded(output);std::cout<<'\n';
    }
}
void export_sum(int id,const std::vector<MacEnergy>& inputs) {
    MacEnergySum sum; for(const auto& e:inputs) sum.add(e);
    std::cout<<"SUM "<<id<<' '<<inputs.size()<<' '<<sum.size()<<' '<<sum.sign()<<' ';encoded(sum.value());std::cout<<'\n';
    for(std::size_t i=0;i<inputs.size();++i) {std::cout<<"TERM "<<id<<' '<<i<<' ';encoded(inputs[i]);std::cout<<'\n';}
    for(std::size_t i=0;i<sum.size();++i) {std::cout<<"COMP "<<id<<' '<<i<<' ';encoded(sum.component(i));std::cout<<'\n';}
}
void expansions() {
    const auto large=MacEnergy::from_double(1e300),tiny=MacEnergy::from_parts(.75,-4096);
    MacEnergySum cancellation;cancellation.add(large);cancellation.add(tiny);cancellation.add(large.negated());
    check(cancellation.size()==1&&cancellation.value().mantissa()==tiny.mantissa()&&cancellation.value().exponent()==tiny.exponent(),"Separated energy vanished in cancellation");
    export_sum(0,{large,tiny,large.negated()});export_sum(1,{tiny,tiny.negated()});
    std::uint64_t seed=UINT64_C(0x53c194aeb167209d);
    for(int id=2;id<34;++id) {
        std::vector<MacEnergy> values;
        for(int i=0;i<64;++i) {
            seed=seed*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
            const auto fraction=seed&((UINT64_C(1)<<52)-1);
            const double m=(seed&(UINT64_C(1)<<63) ? -1 : 1)*(.5+std::ldexp(static_cast<double>(fraction),-53));
            const int e=static_cast<int>((seed>>32)%8193)-4096;values.push_back(MacEnergy::from_parts(m,e));
            if(i%4==0) values.push_back(values.back().negated());
        }
        export_sum(id,values);
    }
}
void accounts() {
    for(int id=0;id<6;++id) {
        const int power=id>=3 ? -4096 : 0;
        std::vector<MacEnergy> values{MacEnergy::from_double(1).shifted(power),MacEnergy::from_double(-1).shifted(power)};
        values.push_back(MacEnergy::from_parts(.5,id%3==0 ? -44 : id%3==1 ? -42 : -4000).shifted(power));
        MacEnergyAccount account;for(const auto& e:values)account.add(e);
        const bool expected=id%3!=1;check(account.within_bound()==expected,"Wide energy account changed its relative operand criterion");
        std::cout<<"ACCOUNT "<<id<<' '<<values.size()<<' '<<account.within_bound()<<' ';encoded(account.residual());std::cout<<' ';encoded(account.bound());std::cout<<'\n';
        for(std::size_t i=0;i<values.size();++i){std::cout<<"ATERM "<<id<<' '<<i<<' ';encoded(values[i]);std::cout<<'\n';}
    }
}
void projections() {
    int id=0;
    for(const auto& e:{MacEnergy::from_parts(.75,-4096),MacEnergy::from_parts(-.75,-4096),MacEnergy::from_parts(.5,-1074),
        MacEnergy::from_parts(.75,-1074),MacEnergy::from_parts(-.75,-1074),MacEnergy::from_parts(.5,-1073),MacEnergy::from_parts(.75,-1073)}) {
        const auto p=e.project();std::cout<<"PROJECT "<<id++<<' ';encoded(e);std::cout<<' '<<p.value<<' '<<p.rounded<<'\n';
        if(p.rounded){bool caught=false;try{static_cast<void>(e.require_representable());}catch(const FluidFatalError&){caught=true;}check(caught,"Unresolved energy projection passed strict narrowing");}
    }
}
void rejected() {
    int caught=0;
    const auto expect=[&](auto f){try{f();}catch(const std::invalid_argument&){++caught;}};
    const auto fatal=[&](auto f){try{f();}catch(const FluidFatalError& e){check(e.code()==FluidFatalCode::numerical_precision,"Wrong wide-energy fatal code");++caught;}};
    expect([]{static_cast<void>(MacEnergy::from_double(std::numeric_limits<double>::quiet_NaN()));});
    expect([]{static_cast<void>(MacEnergy::from_parts(.25,0));});
    expect([]{static_cast<void>(MacEnergy::from_parts(1,0));});
    expect([]{static_cast<void>(MacEnergy::from_parts(0,1));});
    expect([]{static_cast<void>(MacEnergy::kinetic(0,1));});
    expect([]{static_cast<void>(MacEnergy::variance(-1,1));});
    expect([]{static_cast<void>(MacEnergy::from_double(1).shared(1,0));});
    fatal([]{static_cast<void>(MacEnergy::from_parts(.5,1026).project());});
    fatal([]{static_cast<void>((MacExponent(std::numeric_limits<int>::max()) + 1).to_int_exact());});
    MacEnergySum held;held.add(1);expect([&]{held.add(MacEnergy::from_double(std::numeric_limits<double>::infinity()));});
    check(caught==10&&held.size()==1&&held.value().require_representable()==1,"Invalid wide energy input was accepted or changed a held sum");
    std::cout<<"REJECTIONS "<<caught<<'\n';
}
void environment() {
#if defined(_M_X64) || defined(__x86_64__)
    struct Restore {
        std::fenv_t saved{}; unsigned int control;
        Restore() : control(_mm_getcsr()) { check(std::fegetenv(&saved)==0,"Could not save test arithmetic environment"); }
        ~Restore() { static_cast<void>(std::fesetenv(&saved)); _mm_setcsr(control); }
    } restore;
    int rejected_modes=0;
    const auto rejected_mode=[&]{try{require_mac_energy_environment();}catch(const FluidFatalError& e){check(e.code()==FluidFatalCode::numerical_precision,"Wrong arithmetic-environment failure code");++rejected_modes;}};
    for(const int mode:{FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO}) {
        check(std::fesetround(mode)==0,"Could not set private test rounding mode");rejected_mode();
        static_cast<void>(std::fesetenv(&restore.saved));_mm_setcsr(restore.control);
    }
    _mm_setcsr(restore.control|0x8000U);rejected_mode();_mm_setcsr(restore.control);
    _mm_setcsr(restore.control|0x0040U);rejected_mode();_mm_setcsr(restore.control);
    require_mac_energy_environment();check(rejected_modes==5,"Unsupported arithmetic mode was accepted");
    std::cout<<"ENVIRONMENT "<<rejected_modes<<" 1\n";
#else
    throw std::runtime_error("Energy controls require the declared x86-64 environment");
#endif
}
}
int main(){try{
    require_mac_energy_environment();
    std::cout<<std::setprecision(17);
    roundtrips();std::cout<<"PASS exact finite binary64 energy roundtrips\n";
    products();std::cout<<"PASS wide derived kinetic variance and parcel products\n";
    expansions();std::cout<<"PASS exact additive energy expansions across separated exponents\n";
    accounts();std::cout<<"PASS unchanged relative energy accounts above and below binary64 range\n";
    projections();std::cout<<"PASS explicit unresolved projection and strict narrowing\n";
    rejected();std::cout<<"PASS ten invalid energy inputs and immutable held sum\n";
    environment();std::cout<<"PASS read-only environment rejection and private-test restoration\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
