#include "patches.h"

__attribute__((used)) int dummy_sym = 8;

__attribute__((used))
void recomp_crash_dummy(const char* err) {
    *(volatile int*)0 = 0;
}
