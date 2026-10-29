#include <cstdio>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <windows.h>
using Handle=HMODULE;
static Handle open_library(const char* path){auto value=LoadLibraryExA(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!value)std::fprintf(stderr,"LoadLibrary failed: %s (%lu)\n",path,GetLastError());return value;}
static void* lookup(Handle h,const char* name){return reinterpret_cast<void*>(GetProcAddress(h,name));}
#else
#include <dlfcn.h>
using Handle=void*;
static Handle open_library(const char* path){auto value=dlopen(path,RTLD_NOW|RTLD_GLOBAL);if(!value)std::fprintf(stderr,"dlopen failed: %s\n",dlerror());return value;}
static void* lookup(Handle h,const char* name){return dlsym(h,name);}
#endif
template<class T>static T symbol(Handle handle,const char* name){auto value=lookup(handle,name);if(!value){std::fprintf(stderr,"Required Mono export missing: %s\n",name);std::exit(2);}return reinterpret_cast<T>(value);}
int main(int argc,char** argv)
{
    if(argc!=6||sizeof(void*)!=8){std::fprintf(stderr,"mono_runner MONO_LIBRARY MONO_LIB_ROOT MONO_CONFIG_DIR MANAGED_DIR TEST_ASSEMBLY (x64)\n");return 2;}
    auto mono=open_library(argv[1]);if(!mono)return 2;
    symbol<void(*)(const char*,const char*)>(mono,"mono_set_dirs")(argv[2],argv[3]);
    symbol<void(*)(const char*)>(mono,"mono_set_assemblies_path")(argv[4]);
    symbol<void(*)(const char*)>(mono,"mono_config_parse")(nullptr);

#ifndef _WIN32
    auto map=symbol<void(*)(void*,const char*,const char*,const char*,const char*)>(mono,"mono_dllmap_insert");
    const std::string library=argv[1];auto slash=library.find_last_of('/');auto directory=library.substr(0,slash);
    const auto native=directory+"/libmono-native.so",posix=directory+"/libMonoPosixHelper.so";
    map(nullptr,"System.Native",nullptr,native.c_str(),nullptr);
    map(nullptr,"System.Net.Security.Native",nullptr,native.c_str(),nullptr);
    map(nullptr,"MonoPosixHelper",nullptr,posix.c_str(),nullptr);
#endif
    auto domain=symbol<void*(*)(const char*,const char*)>(mono,"mono_jit_init_version")("pacing-credit-harmony-controls","v4.0.30319");
    if(!domain){std::fprintf(stderr,"Mono did not create a domain\n");return 2;}
    auto assembly=symbol<void*(*)(void*,const char*)>(mono,"mono_domain_assembly_open")(domain,argv[5]);
    if(!assembly){std::fprintf(stderr,"Mono could not open control assembly: %s\n",argv[5]);return 2;}
    char* args[]={argv[5],nullptr};
    auto result=symbol<int(*)(void*,void*,int,char**)>(mono,"mono_jit_exec")(domain,assembly,1,args);
    symbol<void(*)(void*)>(mono,"mono_jit_cleanup")(domain);
    if(result==0)std::puts("PASS actual game Mono Harmony pacing controls; no Unity engine or native owner was started.");
    return result;
}
