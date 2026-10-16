#include "mixed_atmospheres/mac_energy.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void encoded(MacEnergy value){std::cout<<value.mantissa()<<' '<<value.exponent();}
MacExponent anchor(){MacExponent value=1;for(int i=0;i<255;++i)value=value.doubled();return value.negated();}
void arithmetic(){
    std::vector<MacExponent> values;
    for(const char* text:{"0","1","-1","2147483647","2147483648","-2147483648","-3049816383","9223372036854775807","-9223372036854775808",
        "9223372036854775808","-9223372036854775809","18446744073709551615","-18446744073709551616",
        "10000000000000000000000000000000000000000001","-9999999999999999999999999999999999999999999"}){
        auto value=MacExponent::from_decimal(text);check(value.to_string()==text,"Exponent decimal roundtrip changed input");values.push_back(std::move(value));}
    std::uint64_t seed=UINT64_C(0x693a17dcb46e0215);
    for(int i=0;i<32;++i){seed=seed*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
        MacExponent value(static_cast<std::int64_t>(seed>>1));const int bits=32+(i%4)*320;
        for(int j=0;j<bits;++j){value=value.doubled();}if(i%2)value=value.negated();values.push_back(std::move(value));}
    for(std::size_t i=0;i<values.size();++i)std::cout<<"INTEGER "<<i<<' '<<values[i]<<' '<<values[i].uses_large_storage()<<'\n';
    std::size_t id=0;
    const auto output=[&](const MacExponent& a,const MacExponent& b){const auto order=a<=>b;
        std::cout<<"IOP "<<id++<<' '<<a<<' '<<b<<' '<<a+b<<' '<<a-b<<' '<<(order<0?-1:order>0?1:0)<<' '<<a.doubled()<<'\n';};
    for(std::size_t i=0;i<15;++i)for(std::size_t j=0;j<15;++j)output(values[i],values[j]);
    for(std::size_t i=15;i<values.size();++i){output(values[i],values[(i+3)%values.size()]);output(values[i],values[i].negated()+5);output(values[i],-127);}
}
void ownership(){
    const auto shared=anchor();const auto held=shared.to_string();std::atomic<bool> correct{true};std::vector<std::thread> workers;
    for(int worker=0;worker<3;++worker)workers.emplace_back([&]{for(int i=0;i<512;++i){
        MacExponent copy=shared;MacExponent moved=std::move(copy);if(copy!=0||moved!=shared)correct=false;
        moved=moved+i;copy=moved;MacExponent final;final=std::move(copy);
        if(copy!=0||final-shared!=i||(final-shared).uses_large_storage()||shared.to_string()!=held)correct=false;
    }});
    for(auto& worker:workers){worker.join();}check(correct&&shared.to_string()==held,"Shared exponent ownership/copy/move changed a source");
    std::cout<<"OWNERSHIP 3 512 "<<shared<<" 1\n";
}
void products(){
    const auto e=anchor();const auto a=MacEnergy::from_parts(.75,e),b=MacEnergy::from_parts(.625,e.doubled()),c=MacEnergy::from_parts(.875,e+e.doubled()+8);
    const auto original=MacEnergy::from_parts(.99960036254833473,MacExponent::from_decimal("-3049816383"));const auto projection=original.project();
    check(!original.is_zero()&&projection.value==0&&projection.rounded,"Former signed32-bit rejection was not represented");
    std::cout<<"FORMER ";encoded(original);std::cout<<' '<<projection.value<<' '<<projection.rounded<<'\n';
    std::cout<<"SKIN 0 3.0517578125e-5 ";encoded(a);std::cout<<' ';encoded(MacEnergy::kinetic(3.0517578125e-5,a));std::cout<<'\n';
    std::cout<<"SVAR 0 ";encoded(a);std::cout<<' ';encoded(b);std::cout<<' ';encoded(MacEnergy::variance(a,b));std::cout<<'\n';
    const auto result=a.shared(b,c);check(result.require_representable()>0&&!result.exponent().uses_large_storage(),"Huge cancelling share exponents did not recover a finite result");
    std::cout<<"SSHARE 0 ";encoded(a);std::cout<<' ';encoded(b);std::cout<<' ';encoded(c);std::cout<<' ';encoded(result);std::cout<<'\n';
}
void export_sum(int id,const std::vector<MacEnergy>& inputs){
    MacEnergySum sum;for(auto value:inputs)sum.add(value);
    std::cout<<"XSUM "<<id<<' '<<inputs.size()<<' '<<sum.size()<<' '<<sum.sign()<<' ';encoded(sum.value());std::cout<<'\n';
    for(std::size_t i=0;i<inputs.size();++i){std::cout<<"XTERM "<<id<<' '<<i<<' ';encoded(inputs[i]);std::cout<<'\n';}
    for(std::size_t i=0;i<sum.size();++i){std::cout<<"XCOMP "<<id<<' '<<i<<' ';encoded(sum.component(i));std::cout<<'\n';}
}
void export_account(int id,const std::vector<MacEnergy>& inputs){
    MacEnergyAccount account;for(auto value:inputs)account.add(value);
    std::cout<<"XACCOUNT "<<id<<' '<<inputs.size()<<' '<<account.terms().size()<<' '<<account.terms().sign()<<' ';encoded(account.residual());std::cout<<' ';encoded(account.bound());std::cout<<' '<<account.within_bound()<<'\n';
    for(std::size_t i=0;i<inputs.size();++i){std::cout<<"XATERM "<<id<<' '<<i<<' ';encoded(inputs[i]);std::cout<<'\n';}
    for(std::size_t i=0;i<account.terms().size();++i){std::cout<<"XACOMP "<<id<<' '<<i<<' ';encoded(account.terms().component(i));std::cout<<'\n';}
}
void expansions(){
    const auto e=anchor();const auto a=MacEnergy::from_parts(.75,e),tiny=MacEnergy::from_parts(.625,e.doubled());
    std::cout<<"ANCHORS "<<e<<' '<<e.doubled()<<'\n';
    export_sum(0,{a,tiny,a.negated()});export_sum(1,{a,a.shifted(-52),a.negated()});export_sum(2,{a,a.negated(),tiny,tiny.negated()});
    export_sum(3,{a,tiny,MacEnergy::from_parts(-.625,e)});export_sum(4,{a,a});
    export_sum(5,{MacEnergy::from_parts(.5+std::ldexp(1.,-53),e),MacEnergy::from_parts(.5,e-53)});
    export_account(0,{a,a.negated(),tiny,tiny.negated()});export_account(1,{a,a.shifted(-50),a.negated()});
    export_account(2,{a,a.shifted(-40),a.negated()});export_account(3,{a,a.negated(),tiny});export_account(4,{tiny});
}
void cascade(){
    auto value=MacEnergy::from_parts(.72004705831217664,-1941);std::cout<<"CASCADE 0 ";encoded(value);std::cout<<'\n';
    for(int i=1;i<=256;++i){value=value.shared(value.magnitude(),MacEnergy::from_double(1));const auto p=value.project();
        check(value.mantissa()>0&&p.rounded&&p.value==0,"Positive cascading share was lost");
        std::cout<<"CASCADE "<<i<<' ';encoded(value);std::cout<<'\n';}
}
void limits(){
    int invalid=0,fatal=0;
    for(const char* text:{"","-","-0","+1","01","--1","1.0"," 1","1 ","x"}){
        try{static_cast<void>(MacExponent::from_decimal(text));}catch(const std::invalid_argument&){++invalid;}}
    auto value=MacExponent(1);for(std::size_t i=1;i<MacExponent::maximum_words*32;++i)value=value.doubled();const auto held=value.to_string();
    check(MacExponent::from_decimal(held)==value,"Maximum exponent decimal roundtrip failed");
    try{static_cast<void>(MacEnergy::from_parts(0,value));}catch(const std::invalid_argument&){++invalid;}
    const auto reject=[&](auto action){try{action();}catch(const FluidFatalError& error){check(error.code()==FluidFatalCode::numerical_precision,"Wrong exponent limit failure code");++fatal;}};
    reject([]{static_cast<void>((MacExponent(std::numeric_limits<int>::max())+1).to_int_exact());});
    reject([]{static_cast<void>((MacExponent(std::numeric_limits<std::int64_t>::max())+1).to_int64_exact());});
    reject([]{static_cast<void>((MacExponent(std::numeric_limits<std::int64_t>::min())-1).to_int64_exact());});
    reject([]{static_cast<void>(MacExponent::from_decimal(std::string(MacExponent::maximum_decimal_characters+1,'9')));});
    reject([&]{static_cast<void>(value.doubled());});
    check(invalid==11&&fatal==5&&value.to_string()==held,"Invalid exponent/narrowing/capacity input was accepted or mutated source");
    std::cout<<"LIMIT "<<MacExponent::maximum_words*32<<' '<<MacExponent::maximum_decimal_characters<<' '<<value<<' '<<invalid<<' '<<fatal<<'\n';
}
}
int main(){try{
    require_mac_energy_environment();std::cout<<std::setprecision(17);
    arithmetic();std::cout<<"PASS exact signed exponent arithmetic and decimal boundaries\n";
    ownership();std::cout<<"PASS immutable shared exponent copy move and concurrent ownership\n";
    products();std::cout<<"PASS huge exponent kinetic variance and cancelling share normalization\n";
    expansions();std::cout<<"PASS exact separated expansions and unchanged operand bounds\n";
    cascade();std::cout<<"PASS 256 cascading scaled products beyond fixed word exponents\n";
    limits();std::cout<<"PASS strict narrowing decimal and bounded allocation reject atomically\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
