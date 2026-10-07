#include "ovl_patches.hpp"
#include "../../RecompiledFuncs/recomp_overlays.inl"

#include "librecomp/overlays.hpp"

void zelda64::register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);

// The runtime only registers the code in the first MB of the ROM. Sands of Time also has code
// at fixed addresses elsewhere in the ROM (the code file and z64rom's uLib at 0x80700000).
void zelda64::load_static_sections(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
    for (size_t i = 0; i < ARRLEN(section_table); i++) {
        const SectionTableEntry& section = section_table[i];
        if ((uint32_t)section.ram_addr < 0x80800000 && section.rom_addr >= 0x1000 + 0x100000) {
            load_overlays(section.rom_addr, section.ram_addr, section.size);
        }
    }
}
