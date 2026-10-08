#ifndef __CRASH_LOG_HPP__
#define __CRASH_LOG_HPP__

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "librecomp/sections.h"

namespace sot::crashlog {
    // Start capturing the console output and handling crashes. Crash logs are written to dir (or fallback_dir
    // if dir isn't writable).
    void init(const std::filesystem::path& dir, const std::filesystem::path& fallback_dir);
    // The recompiled code sections, to describe addresses in the crash logs
    void set_sections(const SectionTableEntry* section_table, size_t count);
    void on_game_start(uint8_t* rdram);
    // Call when the program exits normally (any other exit is a crash)
    void mark_clean_exit();
}

#endif
