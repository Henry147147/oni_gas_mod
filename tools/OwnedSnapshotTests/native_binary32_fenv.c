/* Test-only caller environment. Never linked into or loaded by the game. */
#include <stdint.h>
#include <xmmintrin.h>
#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
EXPORT uint32_t ma_test_mxcsr_get(void) { return _mm_getcsr(); }
EXPORT void ma_test_mxcsr_set(uint32_t value) { _mm_setcsr(value); }
