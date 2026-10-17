#include <funchook.h>
#include "../src/oni_funchook_storage.hpp"
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cstdio>
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
static bool deny_near_claims{},deny_pool_rx{};
static void* tracked_pool_page{};
static unsigned pool_unmaps{},denied_near_allocations{};
static unsigned exact_near_allocations{}, redirected_hint_allocations{};
static std::array<void*,2> raced_pages{};
static unsigned race_pages_remaining{};
extern "C" void* __real_mmap(void*,std::size_t,int,int,int,off_t);
extern "C" int __real_mprotect(void*,std::size_t,int);
extern "C" int __real_munmap(void*,std::size_t);
extern "C" int __wrap_mprotect(void* address,std::size_t size,int protection) {
    if (deny_pool_rx && address==tracked_pool_page && protection==(PROT_READ|PROT_EXEC)) {
        errno=EACCES;return -1;
    }
    return __real_mprotect(address,size,protection);
}
extern "C" int __wrap_munmap(void* address,std::size_t size) {
    if (tracked_pool_page && reinterpret_cast<std::uintptr_t>(address)>=reinterpret_cast<std::uintptr_t>(tracked_pool_page) &&
        reinterpret_cast<std::uintptr_t>(address)<reinterpret_cast<std::uintptr_t>(tracked_pool_page)+4096) ++pool_unmaps;
    return __real_munmap(address,size);
}
extern "C" void* __wrap_mmap(void* address,std::size_t size,int protection,int flags,int descriptor,off_t offset) {
    if (address && (flags & MAP_FIXED_NOREPLACE)) {
        ++exact_near_allocations;
        if (deny_near_claims) { ++denied_near_allocations;errno=ENOMEM;return MAP_FAILED; }
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
#if defined(__linux__)
bool rx_page(void* page) {
    auto* file=std::fopen("/proc/self/maps","r");
    require(file!=nullptr,"read actual page permissions");
    std::array<char,2048> line{};
    bool found{};
    const auto first=reinterpret_cast<std::uintptr_t>(page);
    while (std::fgets(line.data(),static_cast<int>(line.size()),file)) {
        std::size_t start{},end{};std::array<char,5> permissions{};
        if (std::sscanf(line.data(),"%zx-%zx %4s",&start,&end,permissions.data())==3 && start<=first && end>=first+4096) {
            found=std::strcmp(permissions.data(),"r-xp")==0;break;
        }
    }
    std::fclose(file);return found;
}
void borrowed_storage() {
    const auto bytes=fixture(true),second=fixture(false);
    Code code(bytes);
    require(code.size==4096,"borrowed storage control requires the pinned page size");
    require(mprotect(code.memory,code.size,PROT_READ|PROT_WRITE|PROT_EXEC)==0,"write second synthetic function");
    std::memcpy(code.memory+64,second.data(),second.size());
    require(mprotect(code.memory,code.size,PROT_READ|PROT_EXEC)==0,"restore synthetic page permissions");
    std::array<std::uint8_t,4096> baseline{};
    std::memcpy(baseline.data(),code.memory,code.size);
    auto* pool=code.memory+256;
    const auto pool_bytes=code.size-256;
    tracked_pool_page=code.memory;
    deny_near_claims=true;
    // Independent negative control: no borrowed storage, every exact near
    // claim refused. Prepare must fail without touching the target bytes.
    auto* hook=funchook_create();void* target=code.memory;
    require(funchook_prepare(hook,&target,reinterpret_cast<void*>(&hooked))!=0,"no-near-space control unexpectedly prepared");
    require(denied_near_allocations>0,"no-near-space control did not deny any claims");
    require(std::memcmp(code.memory,baseline.data(),code.size)==0,"no-near-space failure altered native bytes");
    require(funchook_destroy(hook)==0,"destroy no-near-space control");
    require(ma_funchook_register_trampoline_buffer(code.memory,256)!=0,"nonzero code accepted as borrowed storage");
    require(ma_funchook_register_trampoline_buffer(pool+1,pool_bytes-1)!=0,"unaligned borrowed storage accepted");
    require(ma_funchook_register_trampoline_buffer(pool,pool_bytes+1)!=0,"cross-page borrowed storage accepted");
    require(ma_funchook_register_trampoline_buffer(pool,16)!=0,"undersized borrowed storage accepted");
    const auto claims=exact_near_allocations;
    for (unsigned round=0;round<96;++round) {
        require(ma_funchook_register_trampoline_buffer(pool,pool_bytes)==0,"register zero RX storage");
        require(ma_funchook_trampoline_capacity()>=28 && ma_funchook_trampoline_capacity()<64,"bounded native trampoline capacity");
        hook=funchook_create();target=code.memory;
        require(funchook_prepare(hook,&target,reinterpret_cast<void*>(&hooked))==0,funchook_error_message(hook));
        const auto address=reinterpret_cast<std::uintptr_t>(target),first=reinterpret_cast<std::uintptr_t>(pool);
        require(address>=first && address<first+pool_bytes && ma_funchook_trampoline_used()==1,"trampoline escaped borrowed storage");
        require(ma_funchook_register_trampoline_buffer(nullptr,0)!=0 &&
                ma_funchook_register_trampoline_buffer(pool,pool_bytes)!=0,"active borrowed storage rebound");
        forward=reinterpret_cast<Function>(target);
        require(funchook_install(hook,0)==0,"install borrowed trampoline");
        auto entry=reinterpret_cast<Function>(code.memory);calls=0;
        require(rx_page(code.memory) && entry(1)==45 && entry(-1)==10 && entry(0)==10 && calls==3,"borrowed trampoline execution/permissions");
        require(funchook_uninstall(hook,0)==0,"uninstall borrowed trampoline");
        require(funchook_destroy(hook)==0,"destroy borrowed trampoline");
        require(std::memcmp(code.memory,baseline.data(),code.size)==0 && rx_page(code.memory),"borrowed trampoline did not restore exact page bytes and RX permissions");
        require(ma_funchook_trampoline_used()==0 && ma_funchook_register_trampoline_buffer(nullptr,0)==0,"release borrowed storage");
    }
    // One entry fits in this subpage. A second, distinct target must reject
    // even if anonymous allocation would otherwise be possible.
    require(ma_funchook_register_trampoline_buffer(pool,128)==0 && ma_funchook_trampoline_capacity()==1,"single-entry bounded pool");
    hook=funchook_create();target=code.memory;
    require(funchook_prepare(hook,&target,reinterpret_cast<void*>(&hooked))==0,"prepare single-entry pool");
    void* other=code.memory+64;
    require(funchook_prepare(hook,&other,reinterpret_cast<void*>(&hooked))!=0 && other==code.memory+64,
            "exhausted borrowed pool accepted another entry or changed output");
    require(std::memcmp(code.memory,baseline.data(),256)==0 && ma_funchook_trampoline_used()==1,"exhausted pool changed native code");
    require(funchook_destroy(hook)==0 && std::memcmp(code.memory,baseline.data(),code.size)==0,"exhausted pool did not restore every byte");
    require(ma_funchook_register_trampoline_buffer(nullptr,0)==0,"release exhausted pool");

    // Propagate a failed final mprotect through destroy; retain the handle and
    // registration until cleanup succeeds. Do not quietly lose ownership.
    require(ma_funchook_register_trampoline_buffer(pool,pool_bytes)==0,"register restoration-failure pool");
    hook=funchook_create();target=code.memory;
    require(funchook_prepare(hook,&target,reinterpret_cast<void*>(&hooked))==0,"prepare restoration-failure pool");
    deny_pool_rx=true;
    require(funchook_destroy(hook)!=0 && ma_funchook_register_trampoline_buffer(nullptr,0)!=0,"failed RX restoration was hidden or ownership was lost");
    deny_pool_rx=false;
    require(funchook_destroy(hook)==0 && rx_page(code.memory) && std::memcmp(code.memory,baseline.data(),code.size)==0,
            "retry did not restore borrowed storage");
    require(ma_funchook_register_trampoline_buffer(nullptr,0)==0,"release restored failure pool");
    require(pool_unmaps==0 && exact_near_allocations==claims,"borrowed storage allocated nearby memory or unmapped image storage");
    deny_near_claims=false;tracked_pool_page=nullptr;
}
#endif
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
        borrowed_storage();
#endif
        auto bytes=fixture(false); bytes[1]=0xfe; reject(bytes); // jmp to self
        bytes=fixture(false); bytes[1]=0; reject(bytes); // jmp into overwritten bytes
        bytes=fixture(false); bytes[0]=0xe2; reject(bytes); // LOOP remains unsupported
        bytes=fixture(false); bytes[0]=0x74; bytes[1]=18; bytes[2]=0x75; bytes[3]=16; reject(bytes);
        bytes=fixture(false); bytes[0]=0x74; bytes[1]=38;
        const std::array<std::uint8_t,15> long_nop{0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x0f,0x1f,0x84,0,0,0,0,0};
        std::memcpy(bytes.data()+2,long_nop.data(),long_nop.size()); reject(bytes);
        std::cout << "PASS: short branches, immutable relocation rejections, near-allocation races, bounded borrowed storage and restoration failures\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
