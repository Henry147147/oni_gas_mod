#!/usr/bin/env python3
"""Pin and repair Linux near-page allocation in the fetched Funchook 1.1.3.

Only downloaded build artifacts change. This complete modification retains
and extends the upstream linking exception, like patch_funchook.py.
"""
from pathlib import Path
import hashlib
import sys

ORIGINAL_SHA256="45673c22287f92486abb10f80affe726557fba921bf73fe9c9802158c57b5f39"
START="""static int get_free_address(funchook_t *funchook, void *func_addr, void *addrs[2])
"""
END="""#define SAFE_JUMP_DISTANCE(X, Y)  ((size_t)(X) - (size_t)(Y)) < (INT32_MAX - page_size)
"""
OLD="""#if defined(CPU_64BIT)
    int loop_cnt;
"""
NEW="""#if defined(CPU_64BIT) && defined(__linux)
    /* Mixed Atmospheres, 2026-10-17: enumerate all nearby gaps and claim
     * an exact page atomically. A hint can be ignored even for a free gap;
     * MAP_FIXED_NOREPLACE never overwrites a concurrent Unity mapping.
     * The upstream linking exception extends to this modified version. */
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
    const size_t target = (size_t)func;
    const size_t distance = INT32_MAX - page_size - 1;
    const size_t low = ROUND_UP(target > distance ? target - distance : page_size, page_size);
    const size_t high = (target > SIZE_MAX - distance ? SIZE_MAX : target + distance) & ~(page_size - 1);
    int pass, last_errno = 0;
    size_t regions = 0, gaps = 0, attempts = 0;
    for (pass = 0; pass < 3; ++pass) {
        memory_map_t mm;
        size_t previous = 0, start, end;
        int rv = memory_map_open(funchook, &mm);
        if (rv != 0) return rv;
        while (memory_map_next(&mm, &start, &end) == 0) {
            ++regions;
            size_t first = previous > low ? previous : low;
            size_t last = start >= page_size ? start - page_size : 0;
            if (last > high) last = high;
            if (first <= last) {
                ++gaps;
                const int descending = last < target;
                size_t address = descending ? last : first;
                for (;;) {
                    ++attempts;
                    void *page = mmap((void*)address, page_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
                    if (page != MAP_FAILED) {
                        if ((size_t)page != address) {
                            /* Old kernels can ignore an unknown flag. Never
                             * accept a relocated page or use MAP_FIXED. */
                            munmap(page, page_size);
                            memory_map_close(&mm);
                            funchook_set_error_message(funchook, "Kernel ignored exact non-replacing near-page allocation");
                            return FUNCHOOK_ERROR_MEMORY_ALLOCATION;
                        }
                        *page_out = (funchook_page_t*)page;
                        memory_map_close(&mm);
                        return 0;
                    }
                    last_errno = errno;
                    if (errno != EEXIST) break;
                    /* This gap changed after /proc/self/maps was read. Try
                     * its remaining pages before considering the next gap. */
                    if (descending) {
                        if (address - first < page_size) break;
                        address -= page_size;
                    } else {
                        if (last - address < page_size) break;
                        address += page_size;
                    }
                }
            }
            previous = end;
            if (start > high) break;
        }
        memory_map_close(&mm);
    }
    funchook_set_error_message(funchook, "Cannot claim near page: func=%p range=%p-%p regions=%zu gaps=%zu attempts=%zu errno=%d", func, (void*)low, (void*)high, regions, gaps, attempts, last_errno);
    return FUNCHOOK_ERROR_MEMORY_ALLOCATION;
#elif defined(CPU_64BIT)
    int loop_cnt;
"""
REPLACEMENTS=((START,"#if !defined(__linux)\n"+START),(END,END+"#endif /* !defined(__linux) */\n"),(OLD,NEW))

def patch(path: Path):
    original=path.read_text()
    baseline=original
    for old,new in reversed(REPLACEMENTS):
        if new in baseline: baseline=baseline.replace(new,old,1)
    if hashlib.sha256(baseline.encode()).hexdigest()!=ORIGINAL_SHA256:
        raise ValueError("Unrecognized Funchook Unix source; refusing allocation patch")
    result=baseline
    for old,new in REPLACEMENTS:
        if result.count(old)!=1: raise ValueError("Funchook allocation patch anchor differs")
        result=result.replace(old,new,1)
    if result!=original: path.write_text(result)

if __name__=="__main__": patch(Path(sys.argv[1]))
