// recomp_printf, for mods (like Zelda64Recomp's)

#include "patches.h"
#include "misc_funcs.h"

static void* prout_printf(void* dst, const char* fmt, size_t size) {
    recomp_puts(fmt, size);
    return (void*)1;
}

RECOMP_EXPORT int recomp_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int ret = _Printf(&prout_printf, NULL, fmt, args);

    va_end(args);

    return ret;
}
