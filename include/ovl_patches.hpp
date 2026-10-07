#ifndef __OVL_PATCHES_HPP__
#define __OVL_PATCHES_HPP__

#include <cstdint>
#include "recomp.h"

namespace zelda64 {
    void register_overlays();
    void register_patches();
    // Registers the code that's always loaded (the code file and z64rom's uLib)
    void load_static_sections(uint8_t* rdram, recomp_context* ctx);
}

#endif
