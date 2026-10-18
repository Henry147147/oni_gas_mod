#include "mixed_atmospheres/mac_energy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace ma::experimental;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool equal(MacEnergy a,MacEnergy b){return a.mantissa()==b.mantissa() && a.exponent()==b.exponent();}
struct Reference {
    std::vector<MacEnergy> terms;
    void add(MacEnergy value){
        if(value.is_zero())return;
        std::vector<MacEnergy> next;
        for(const auto& term:terms){const auto pair=add_mac_energy(value,term);if(!pair.low.is_zero())next.push_back(pair.low);value=pair.high;}
        if(!value.is_zero())next.push_back(value);
        terms=std::move(next);
    }
    MacEnergy value()const{MacEnergy total;for(const auto& term:terms)total=add_mac_energy(total,term).high;return total;}
};
std::uint64_t prefixes=0;
void compare(const MacEnergySum& actual,const Reference& expected){
    check(actual.size()==expected.terms.size(),"Native/full-range expansion component count differs");
    for(std::size_t i=0;i<actual.size();++i)check(equal(actual.component(i),expected.terms[i]),"Native/full-range expansion component differs");
    check(equal(actual.value(),expected.value()),"Native/full-range expansion estimate differs");
    const int sign=expected.terms.empty()?0:expected.terms.back().mantissa()<0?-1:1;
    check(actual.sign()==sign,"Native/full-range expansion sign differs");
    ++prefixes;
}
void run(const std::vector<MacEnergy>& inputs){
    MacEnergySum actual,direct;Reference expected;bool direct_possible=true;
    for(const auto& input:inputs){
        actual.add(input);expected.add(input);compare(actual,expected);
        if(direct_possible){
            if(input.exponent() < -1073 || input.exponent() > 1024)direct_possible=false;
            else {
                const auto p=input.project();
                if(p.rounded)direct_possible=false;
                else {direct.add(p.value);compare(direct,expected);}
            }
        }
    }
    auto copy=actual;compare(copy,expected);
    auto moved=std::move(copy);compare(moved,expected);
    MacEnergySum assigned;assigned=moved;compare(assigned,expected);
    MacEnergySum move_assigned;move_assigned=std::move(assigned);compare(move_assigned,expected);
    for(auto i=inputs.rbegin();i!=inputs.rend();++i){moved.add(i->negated());expected.add(i->negated());compare(moved,expected);}
    check(moved.size()==0,"Complete exact cancellation left an expansion");
}
}
int main(){try{
    require_mac_energy_environment();
    std::uint64_t seed=UINT64_C(0x67da3d978af6114b);
    for(std::uint64_t power=0;power<2047;++power)for(int sample=0;sample<8;++sample){
        std::vector<MacEnergy> values;
        for(int k=0;k<24;++k){
            seed=seed*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
            const auto biased=sample<4?power:((seed>>48)%2047);
            const auto bits=(seed&UINT64_C(0x800fffffffffffff))|(biased<<52);
            values.push_back(MacEnergy::from_double(std::bit_cast<double>(bits)));
            if(k%5==0)values.push_back(values.back().negated());
        }
        run(values);
    }
    const auto minimum=std::numeric_limits<double>::denorm_min(),maximum=std::numeric_limits<double>::max();
    run({MacEnergy::from_double(maximum),MacEnergy::from_double(maximum),MacEnergy::from_double(-maximum),MacEnergy::from_double(minimum)});
    run({MacEnergy::from_double(1),MacEnergy::from_parts(.75,-1200),MacEnergy::from_double(-1)});
    run({MacEnergy::from_double(1),MacEnergy::from_parts(.75,1200),MacEnergy::from_double(-1)});
    run({MacEnergy::from_parts(.5,-500),MacEnergy::from_parts(.5,-250),MacEnergy::from_parts(.5,0),
        MacEnergy::from_parts(.5,250),MacEnergy::from_parts(.5,500),MacEnergy::from_parts(.5,750)});
    const auto huge=MacExponent::from_decimal("100000000000000000000000000000000000000000000000000000");
    run({MacEnergy::from_double(1),MacEnergy::from_parts(.75,huge),MacEnergy::from_parts(.5,huge.negated()),MacEnergy::from_double(-1)});
    MacEnergySum direct;Reference expected;
    for(const double x:{1.,1e-20,-1.,maximum,maximum,-maximum,minimum,-minimum}){direct.add(x);expected.add(MacEnergy::from_double(x));compare(direct,expected);}
    for(const double bad:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool rejected=false;try{direct.add(bad);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Direct nonfinite sum operand was accepted");compare(direct,expected);
    }
    std::cout<<"PASS "<<prefixes<<" exact reference prefixes, copies, moves and cancellation across every binary64 exponent bin and full-range fallbacks\n";
    std::cout<<"SIZE "<<sizeof(MacEnergySum)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
