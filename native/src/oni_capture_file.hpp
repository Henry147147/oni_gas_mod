#pragma once

#include <cstdio>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

namespace ma::oni {
// msvcrt's fopen ignores C11's exclusive-create flag. Preserve captures on
// Windows with an atomic exclusive descriptor open before constructing FILE.
inline std::FILE* capture_file(const char* path,bool binary=true) noexcept {
#if defined(_WIN32)
    const auto descriptor=_open(path,_O_CREAT|_O_EXCL|_O_WRONLY|(binary?_O_BINARY:_O_TEXT),_S_IREAD|_S_IWRITE);
    if(descriptor<0)return nullptr;
    auto* file=_fdopen(descriptor,binary?"wb":"w");
    if(!file)(void)_close(descriptor);
    return file;
#else
    return std::fopen(path,binary?"wbx":"wx");
#endif
}
}
