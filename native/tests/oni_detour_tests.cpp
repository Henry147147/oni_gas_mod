#include <funchook.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
using Function = int (*)(int);
Function forward{};
int calls{};
int hooked(int arg) { ++calls; return forward(arg)+3; }
struct Code {
    std::uint8_t* memory{};
    std::size_t size=4096;
    explicit Code(const std::array<std::uint8_t,64>& bytes) {
#if defined(_WIN32)
        memory=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,size,MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE));
#else
        size=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        memory=static_cast<std::uint8_t*>(mmap(nullptr,size,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS,-1,0));
        if (memory==MAP_FAILED) memory=nullptr;
#endif
        require(memory!=nullptr,"allocate synthetic code");
        std::memcpy(memory,bytes.data(),bytes.size());
#if defined(_WIN32)
        DWORD old{};
        require(VirtualProtect(memory,size,PAGE_EXECUTE_READ,&old)!=0,"protect synthetic code");
        FlushInstructionCache(GetCurrentProcess(),memory,bytes.size());
#else
        require(mprotect(memory,size,PROT_READ | PROT_EXEC)==0,"protect synthetic code");
#endif
    }
    ~Code() {
#if defined(_WIN32)
        if (memory) VirtualFree(memory,0,MEM_RELEASE);
#else
        if (memory) munmap(memory,size);
#endif
    }
};
std::array<std::uint8_t,64> fixture(bool conditional) {
    std::array<std::uint8_t,64> bytes{};
    bytes.fill(0x90);
    if (conditional) {
        bytes[0]=0x85;
#if defined(_WIN32)
        bytes[1]=0xc9; // test ecx,ecx (Microsoft x64 argument)
#else
        bytes[1]=0xff; // test edi,edi (System V argument)
#endif
        bytes[2]=0x7e; bytes[3]=16; // jle +20
        bytes[4]=0xb8; bytes[5]=42; bytes[6]=0; bytes[7]=0; bytes[8]=0; bytes[9]=0xc3;
    } else { bytes[0]=0xeb; bytes[1]=18; }
    bytes[20]=0xb8; bytes[21]=7; bytes[22]=0; bytes[23]=0; bytes[24]=0; bytes[25]=0xc3;
    return bytes;
}
void run(bool conditional) {
    auto bytes=fixture(conditional);
    Code code(bytes);
    auto entry=reinterpret_cast<Function>(code.memory);
    require(entry(1)==(conditional ? 42 : 7) && entry(-1)==7 && entry(0)==7,"synthetic baseline");
    auto* hook=funchook_create();
    require(hook!=nullptr,"create synthetic detour");
    void* trampoline=code.memory;
    require(funchook_prepare(hook,&trampoline,reinterpret_cast<void*>(&hooked))==0,funchook_error_message(hook));
    forward=reinterpret_cast<Function>(trampoline);
    require(funchook_install(hook,0)==0,"install synthetic detour");
    calls=0;
    require(entry(1)==(conditional ? 45 : 10) && entry(-1)==10 && entry(0)==10 && calls==3,"relocated branch/return/flags");
    require(funchook_uninstall(hook,0)==0,"uninstall synthetic detour");
    require(std::memcmp(code.memory,bytes.data(),bytes.size())==0 && entry(1)==(conditional ? 42 : 7),"restore original synthetic bytes");
    require(funchook_destroy(hook)==0,"destroy synthetic detour");
}
void reject(std::array<std::uint8_t,64> bytes) {
    Code code(bytes);
    auto* hook=funchook_create();
    void* target=code.memory;
    require(funchook_prepare(hook,&target,reinterpret_cast<void*>(&hooked))!=0,"unsupported relocation accepted");
    require(std::memcmp(code.memory,bytes.data(),bytes.size())==0,"failed prepare altered code");
    require(funchook_destroy(hook)==0,"destroy rejected detour");
}
}
int main() {
    try {
        run(true); run(false);
        auto bytes=fixture(false); bytes[1]=0xfe; reject(bytes); // jmp to self
        bytes=fixture(false); bytes[1]=0; reject(bytes); // jmp into overwritten bytes
        bytes=fixture(false); bytes[0]=0xe2; reject(bytes); // LOOP remains unsupported
        bytes=fixture(false); bytes[0]=0x74; bytes[1]=18; bytes[2]=0x75; bytes[3]=16; reject(bytes);
        bytes=fixture(false); bytes[0]=0x74; bytes[1]=38;
        const std::array<std::uint8_t,15> long_nop{0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x0f,0x1f,0x84,0,0,0,0,0};
        std::memcpy(bytes.data()+2,long_nop.data(),long_nop.size()); reject(bytes);
        std::cout << "PASS: short Jcc/JMP forwarding, branch outcomes, restoration and five immutable relocation rejections\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
