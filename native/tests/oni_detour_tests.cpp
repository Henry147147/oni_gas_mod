#include <funchook.h>
#include <array>
#include <cerrno>
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

#if defined(__linux__)
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
static bool ignored_hints{};
static unsigned exact_near_allocations{}, redirected_hint_allocations{};
static std::array<void*,2> raced_pages{};
static unsigned race_pages_remaining{};
extern "C" void* __real_mmap(void*,std::size_t,int,int,int,off_t);
extern "C" void* __wrap_mmap(void* address,std::size_t size,int protection,int flags,int descriptor,off_t offset) {
    if (address && (flags & MAP_FIXED_NOREPLACE)) {
        ++exact_near_allocations;
        if (race_pages_remaining) {
            void* occupied=__real_mmap(address,size,protection,flags,descriptor,offset);
            if (occupied!=MAP_FAILED) {
                raced_pages[--race_pages_remaining]=occupied;std::memset(occupied,0xa5,size);
                errno=EEXIST;return MAP_FAILED;
            }
        }
    }
    if (ignored_hints && address && !(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        ++redirected_hint_allocations;
        return __real_mmap(nullptr,size,protection,flags | MAP_32BIT,descriptor,offset);
    }
    return __real_mmap(address,size,protection,flags,descriptor,offset);
}
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
#if defined(__linux__)
        // Reproduce a kernel ignoring a near-address hint. The old allocator
        // repeats that hint and fails; exact non-replacing claims must work.
        ignored_hints=true;
        void* redirected=mmap(reinterpret_cast<void*>(0x700000000000ULL),4096,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
        require(redirected!=MAP_FAILED && reinterpret_cast<std::uintptr_t>(redirected)<0x80000000ULL,"near-allocation negative control did not ignore its hint");
        munmap(redirected,4096);
        const auto exact_before=exact_near_allocations;
        race_pages_remaining=2;
        run(true); run(false);
        require(redirected_hint_allocations==1 && exact_near_allocations>exact_before,"detours did not use atomic non-replacing near-page claims");
        require(race_pages_remaining==0,"near-allocation control did not exercise concurrent page claims");
        for (auto* page : raced_pages) {
            const auto* bytes=static_cast<const std::uint8_t*>(page);
            for (std::size_t i=0;i<4096;++i) require(bytes[i]==0xa5,"trampoline allocator overwrote a concurrently occupied page");
            munmap(page,4096);
        }
        ignored_hints=false;
#endif
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
