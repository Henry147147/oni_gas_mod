#pragma once
#include <cstdio>
#include <cstdlib>
namespace ma::oni {
inline void windows_lifecycle_trace(const char* stage) noexcept {
#if defined(_WIN32)
    const auto* path=std::getenv("MA_WINDOWS_SHUTDOWN_TRACE");
    if(!path||!*path)return;
    if(auto* file=std::fopen(path,"a")) {
        std::fprintf(file,"%s\n",stage);std::fclose(file);
    }
#else
    (void)stage;
#endif
}
}
