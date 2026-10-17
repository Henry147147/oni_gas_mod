#!/usr/bin/env python3
"""Pin Linux near-page allocation and bounded borrowed storage in Funchook 1.1.3.

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
BORROWED = """
#if defined(CPU_X86_64) && defined(__linux)
/* Mixed Atmospheres, 2026-10-17. The caller verifies an unused, zero-filled
 * tail of a private RX mapping. Never allocate outside this bounded buffer
 * while registered, never remove execute permission from its containing
 * native code page, and never munmap memory belonging to that image.
 * The upstream linking exception extends to this modified version. */
static funchook_page_t *ma_buffer;
static size_t ma_buffer_bytes;
static int ma_buffer_in_use;
static unsigned char ma_buffer_lock;

static void ma_lock_buffer(void)
{
    while (__atomic_test_and_set(&ma_buffer_lock, __ATOMIC_ACQUIRE)) { }
}

static void ma_unlock_buffer(void)
{
    __atomic_clear(&ma_buffer_lock, __ATOMIC_RELEASE);
}

int ma_funchook_register_trampoline_buffer(void *buffer, size_t bytes)
{
    size_t i;
    int result = FUNCHOOK_ERROR_MEMORY_ALLOCATION;
    ma_lock_buffer();
    if (ma_buffer_in_use) goto done;
    if (buffer == NULL && bytes == 0) {
        ma_buffer = NULL;
        ma_buffer_bytes = 0;
        result = 0;
        goto done;
    }
    if (ma_buffer != NULL || buffer == NULL || ((size_t)buffer & 15) != 0 ||
        bytes < offsetof(funchook_page_t, entries) + sizeof(funchook_entry_t) ||
        bytes > page_size || ((size_t)buffer & (page_size - 1)) > page_size - bytes)
        goto done;
    for (i = 0; i < bytes; ++i) if (((unsigned char*)buffer)[i] != 0) goto done;
    ma_buffer = (funchook_page_t*)buffer;
    ma_buffer_bytes = bytes;
    result = 0;
done:
    ma_unlock_buffer();
    return result;
}

size_t ma_funchook_trampoline_capacity(void)
{
    size_t capacity;
    ma_lock_buffer();
    capacity = ma_buffer == NULL ? 0 :
        (ma_buffer_bytes - offsetof(funchook_page_t, entries)) / sizeof(funchook_entry_t);
    ma_unlock_buffer();
    return capacity;
}

/* Called only by the owning, quiescent Funchook handle. */
size_t ma_funchook_trampoline_used(void)
{
    size_t used;
    ma_lock_buffer();
    used = ma_buffer_in_use ? ma_buffer->used : 0;
    ma_unlock_buffer();
    return used;
}

size_t ma_funchook_page_capacity(funchook_page_t *page)
{
    return page == ma_buffer ? ma_funchook_trampoline_capacity() :
        (page_size - offsetof(funchook_page_t, entries)) / sizeof(funchook_entry_t);
}

static int ma_buffer_protection(funchook_t *funchook, int protection)
{
    void *start = (void*)ROUND_DOWN((size_t)ma_buffer, page_size);
    if (mprotect(start, page_size, protection) == 0) return 0;
    funchook_set_error_message(funchook, "Cannot protect borrowed trampoline page %p: errno=%d", start, errno);
    return FUNCHOOK_ERROR_MEMORY_FUNCTION;
}
#endif
"""

ALLOCATE = """#if defined(CPU_X86_64) && defined(__linux)
    ma_lock_buffer();
    if (ma_buffer != NULL) {
        int rv = FUNCHOOK_ERROR_MEMORY_ALLOCATION;
        if (ma_buffer_in_use || !funchook_page_avail(funchook, ma_buffer, 0, func, disp)) {
            funchook_set_error_message(funchook, "Verified trampoline buffer is full, already owned, or unreachable");
        } else {
            rv = ma_buffer_protection(funchook, PROT_READ | PROT_WRITE | PROT_EXEC);
            if (rv == 0) {
                ma_buffer_in_use = 1;
                *page_out = ma_buffer;
            }
        }
        ma_unlock_buffer();
        return rv;
    }
    ma_unlock_buffer();
#endif
"""

FREE = """#if defined(CPU_X86_64) && defined(__linux)
    if (page == ma_buffer) {
        int rv;
        ma_lock_buffer();
        rv = ma_buffer_protection(funchook, PROT_READ | PROT_WRITE | PROT_EXEC);
        if (rv == 0) {
            memset(ma_buffer, 0, ma_buffer_bytes);
            rv = ma_buffer_protection(funchook, PROT_READ | PROT_EXEC);
            if (rv == 0) ma_buffer_in_use = 0;
        }
        ma_unlock_buffer();
        return rv;
    }
#endif
"""

# Keep the original repair first, so exact reversal also upgrades existing
# build caches containing only that repair. Unknown dependency edits reject.
REPLACEMENTS=((START,"#if !defined(__linux)\n"+START),
              (END,END+"#endif /* !defined(__linux) */\n"),(OLD,NEW),
              ("const size_t page_size = PAGE_SIZE;\n", "const size_t page_size = PAGE_SIZE;\n" + BORROWED),
              ("int funchook_page_alloc(funchook_t *funchook, funchook_page_t **page_out, uint8_t *func, ip_displacement_t *disp)\n{\n",
               "int funchook_page_alloc(funchook_t *funchook, funchook_page_t **page_out, uint8_t *func, ip_displacement_t *disp)\n{\n" + ALLOCATE),
              ("int funchook_page_free(funchook_t *funchook, funchook_page_t *page)\n{\n",
               "int funchook_page_free(funchook_t *funchook, funchook_page_t *page)\n{\n" + FREE),
              ("int funchook_page_protect(funchook_t *funchook, funchook_page_t *page)\n{\n",
               "int funchook_page_protect(funchook_t *funchook, funchook_page_t *page)\n{\n"
               "#if defined(CPU_X86_64) && defined(__linux)\n"
               "    if (page == ma_buffer) return ma_buffer_protection(funchook, PROT_READ | PROT_EXEC);\n#endif\n"),
              ("int funchook_page_unprotect(funchook_t *funchook, funchook_page_t *page)\n{\n",
               "int funchook_page_unprotect(funchook_t *funchook, funchook_page_t *page)\n{\n"
               "#if defined(CPU_X86_64) && defined(__linux)\n"
               "    if (page == ma_buffer) return ma_buffer_protection(funchook, PROT_READ | PROT_WRITE | PROT_EXEC);\n#endif\n"),
              ("#include <stdint.h>\n", "#include <stdint.h>\n#include <stddef.h>\n"))

CORE_SHA256 = "7214ec9e8fff4ca39cc604c4cb8f3a56f64ebbff825605747c6c5b7567b95e7a"
CORE_REPLACEMENTS = (
    ('#include "funchook_internal.h"\n', '#include "funchook_internal.h"\n'
     '#if defined(CPU_X86_64) && defined(__linux)\n'
     'extern size_t ma_funchook_page_capacity(funchook_page_t *page);\n'
     '#define MA_PAGE_CAPACITY(page) ma_funchook_page_capacity(page)\n'
     '#else\n#define MA_PAGE_CAPACITY(page) num_entries_in_page\n#endif\n'),
    ('page->used < num_entries_in_page &&', 'page->used < MA_PAGE_CAPACITY(page) &&'),
    ('        funchook_page_unprotect(funchook, page);\n',
     '        {\n            int rv = funchook_page_unprotect(funchook, page);\n'
     '            if (rv != 0) return rv;\n        }\n'),
    ('    for (page = funchook->page_list; page != NULL; page = page_next) {\n'
     '        page_next = page->next;\n        funchook_page_free(funchook, page);\n    }\n',
     '    for (page = funchook->page_list; page != NULL; page = page_next) {\n'
     '        int rv;\n        page_next = page->next;\n'
     '        rv = funchook_page_free(funchook, page);\n'
     '        if (rv != 0) return rv;\n'
     '        funchook->page_list = page_next;\n    }\n'),
)

def patch(path: Path, replacements=REPLACEMENTS, digest=ORIGINAL_SHA256):
    original=path.read_text()
    baseline=original
    for old,new in reversed(replacements):
        if new in baseline: baseline=baseline.replace(new,old,1)
    if hashlib.sha256(baseline.encode()).hexdigest()!=digest:
        raise ValueError("Unrecognized Funchook Unix source; refusing allocation patch")
    result=baseline
    for old,new in replacements:
        if result.count(old)!=1: raise ValueError("Funchook allocation patch anchor differs")
        result=result.replace(old,new,1)
    if result!=original: path.write_text(result)

if __name__=="__main__":
    patch(Path(sys.argv[1]))
    patch(Path(sys.argv[2]), CORE_REPLACEMENTS, CORE_SHA256)
