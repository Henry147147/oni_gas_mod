#include "../src/oni_domain.hpp"
#include <array>
#include <cfenv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
unsigned short control() { unsigned short value=0;__asm__ volatile("fnstcw %0" : "=m"(value));return value; }
void precision(unsigned short bits) {
    const auto value=static_cast<unsigned short>((control() & ~0x0300U) | bits);
    __asm__ volatile("fldcw %0" : : "m"(value));
}
#else
unsigned short control() { return 0; }
void precision(unsigned short) {}
#endif
struct Environment {
    std::fenv_t state{};unsigned int sse=_mm_getcsr();
    Environment() { require(std::fegetenv(&state)==0,"Could not preserve test arithmetic environment"); }
    ~Environment() { (void)std::fesetenv(&state);_mm_setcsr(sse); }
};
std::vector<std::uint8_t> initial_image() {
    const MAOniDomainConfig config{40,2,1,64,10,10,3,2,1,0};
    std::array<MAOniDomainInitialCell,6> cells{};
    const std::array masses{1100.0,1100.03,1099.97,1100.01,1099.98,1100.02};
    const std::array temperatures{300.003,299.998,300.001,299.999,300.002,299.997};
    for (std::size_t i=0;i<cells.size();++i) {
        cells[i].struct_size=56;cells[i].abi_version=1;cells[i].masses[3]=masses[i];cells[i].temperature_k=temperatures[i];
    }
    ma::oni::start_domain(config,1,{},0,cells);
    std::vector<MAOniDomainCell> output(6);MAOniDomainStatus state{};ma::oni::copy_domain(state,output.data(),6);
    auto image=ma::oni::checkpoint_domain(state.session,0);ma::oni::shutdown_domain();return image;
}
}
int main(int argc,char** argv) {
    try {
        Environment environment;precision(0x0300U);require(std::fesetround(FE_TONEAREST)==0,"Could not set reference rounding");
        _mm_setcsr(_mm_getcsr() & ~0xe040U);const auto source=initial_image();
        if (argc==3 && std::string(argv[1])=="--write-fixture") {
            std::ofstream file(argv[2],std::ios::binary);file.write(reinterpret_cast<const char*>(source.data()),static_cast<std::streamsize>(source.size()));
            require(file.good(),"Could not write synthetic arithmetic fixture");
        } else require(argc==1,"Unknown arithmetic-control test arguments");
        std::vector<std::vector<std::uint8_t>> expected;auto reference=source;
        for (int frame=0;frame<16;++frame) { reference=ma::oni::advance_domain_checkpoint(reference,.2F);expected.push_back(reference); }
        for (const auto bits : {0x0200U,0x0300U}) {
            precision(static_cast<unsigned short>(bits));require(std::fesetround(FE_DOWNWARD)==0,"Could not set caller rounding");
            _mm_setcsr(_mm_getcsr() | 0x8040U);std::feraiseexcept(FE_INVALID|FE_INEXACT);
            const auto before_control=control();const auto before_sse=_mm_getcsr();const auto before_flags=std::fetestexcept(FE_ALL_EXCEPT);
            auto candidate=source;
            for (std::size_t frame=0;frame<expected.size();++frame) {
                candidate=ma::oni::advance_domain_checkpoint(candidate,.2F);
                require(control()==before_control && _mm_getcsr()==before_sse && std::fegetround()==FE_DOWNWARD &&
                    std::fetestexcept(FE_ALL_EXCEPT)==before_flags,"Native replay changed the caller's precision, rounding, flags or SSE controls");
                require(candidate==expected[frame],"Native water continuation inherited the caller's x87 precision");
            }
            auto malformed=source;malformed.at(malformed.size()-1)^=1;bool rejected=false;
            try { (void)ma::oni::advance_domain_checkpoint(malformed,.2F); } catch (const std::exception&) { rejected=true; }
            require(rejected && control()==before_control && _mm_getcsr()==before_sse && std::fegetround()==FE_DOWNWARD &&
                std::fetestexcept(FE_ALL_EXCEPT)==before_flags,"Rejected native replay changed the caller's arithmetic environment");
        }
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        std::cout << "PASS identical 16-frame water continuation under both x87 precision controls, caller rounding/flags/SSE restoration and rejection restoration\n";
#else
        std::cout << "PASS identical 16-frame water continuation with caller rounding/flags/SSE restoration and rejection restoration; x87 precision selector unavailable\n";
#endif
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
