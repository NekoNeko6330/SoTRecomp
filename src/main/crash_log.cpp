// Crash logs: every crash (a function that wasn't found in the ROM, a jump table out of bounds, a
// native crash, an error reported by the game itself, ...) is written to the "crash_logs" folder,
// so that it can be reported and fixed.
//
// The runtime reports its errors on stdout/stderr and then exits, so the console output is captured
// (and still forwarded to the console) and the report is written when the program exits abnormally.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#endif

#include "SDL.h"

#include "recomp.h"
#include "librecomp/sections.h"
#include "crash_log.hpp"
#include "zelda_config.h"

#ifndef SOT_BUILD_VERSION
#define SOT_BUILD_VERSION "unknown"
#endif

namespace {
    constexpr size_t max_output_lines = 400;

    std::filesystem::path log_dir;
    std::filesystem::path fallback_log_dir;

    std::mutex output_mutex;
    std::deque<std::string> output_lines;
    std::string partial_line;

    std::atomic<bool> clean_exit = false;
    std::atomic<bool> report_written = false;
    std::atomic<bool> game_started = false;
    uint8_t* rdram_ptr = nullptr;

    // Overlays loaded by the game: ram address -> (rom address, size)
    struct LoadedOverlay {
        uint32_t rom;
        uint32_t size;
    };
    std::mutex overlays_mutex;
    std::map<uint32_t, LoadedOverlay> loaded_overlays;
    const SectionTableEntry* sections = nullptr;
    size_t num_sections = 0;

    // Console capture: the output is forwarded to the original stream and kept in output_lines.
    struct CapturedStream {
        int original_fd = -1;
        int read_fd = -1;
        std::thread thread;
    };
    CapturedStream captured_stdout;
    CapturedStream captured_stderr;

    void add_output(const char* data, size_t size) {
        std::lock_guard lock{ output_mutex };
        for (size_t i = 0; i < size; i++) {
            char c = data[i];
            if (c == '\n') {
                output_lines.emplace_back(std::move(partial_line));
                partial_line.clear();
                if (output_lines.size() > max_output_lines) {
                    output_lines.pop_front();
                }
            }
            else if (c != '\r') {
                partial_line += c;
            }
        }
    }

#ifdef _WIN32
    int sys_read(int fd, char* buf, unsigned size) { return _read(fd, buf, size); }
    int sys_write(int fd, const char* buf, unsigned size) { return _write(fd, buf, size); }
#else
    int sys_read(int fd, char* buf, unsigned size) { return (int)read(fd, buf, size); }
    int sys_write(int fd, const char* buf, unsigned size) { return (int)write(fd, buf, size); }
#endif

    void capture_thread(CapturedStream* stream) {
        char buffer[4096];
        while (true) {
            int count = sys_read(stream->read_fd, buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            add_output(buffer, (size_t)count);
            if (stream->original_fd >= 0) {
                sys_write(stream->original_fd, buffer, (unsigned)count);
            }
        }
    }

    void capture_stream(FILE* file, CapturedStream& stream) {
#ifdef _WIN32
        // GUI programs have no valid stdout/stderr unless a console was attached
        if (_fileno(file) < 0) {
            FILE* dummy;
            freopen_s(&dummy, "NUL", "w", file);
        }
        int fd = _fileno(file);
        if (fd < 0) {
            return;
        }
        int pipe_fds[2];
        if (_pipe(pipe_fds, 65536, _O_BINARY) != 0) {
            return;
        }
        stream.original_fd = _dup(fd);
        _dup2(pipe_fds[1], fd);
        _close(pipe_fds[1]);
#else
        int fd = fileno(file);
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            return;
        }
        stream.original_fd = dup(fd);
        dup2(pipe_fds[1], fd);
        close(pipe_fds[1]);
#endif
        stream.read_fd = pipe_fds[0];
        setvbuf(file, nullptr, _IONBF, 0);
        stream.thread = std::thread{ capture_thread, &stream };
        stream.thread.detach();
    }

    std::string timestamp(const char* format) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), format, &tm);
        return buf;
    }

    std::string hex32(uint32_t value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", value);
        return buf;
    }

    // Describe where a game address is: which section it belongs to, and its address in the original section,
    // which is how functions are named in the recompiled code (e.g. ovl_actor_0011_80800A2C).
    std::string describe_address(uint32_t addr) {
        std::lock_guard lock{ overlays_mutex };
        auto section_for_rom = [](uint32_t rom) -> const SectionTableEntry* {
            for (size_t i = 0; i < num_sections; i++) {
                if (sections[i].rom_addr == rom) {
                    return &sections[i];
                }
            }
            return nullptr;
        };
        for (const auto& [ram, ovl] : loaded_overlays) {
            if (addr >= ram && addr < ram + ovl.size) {
                uint32_t offset = addr - ram;
                std::string ret = "in the overlay at ROM " + hex32(ovl.rom) + " (loaded at " + hex32(ram) + "), offset " + hex32(offset);
                const SectionTableEntry* section = section_for_rom(ovl.rom);
                if (section != nullptr) {
                    ret += ", original address " + hex32((uint32_t)section->ram_addr + offset);
                }
                return ret;
            }
        }
        for (size_t i = 0; i < num_sections; i++) {
            const SectionTableEntry& section = sections[i];
            if ((uint32_t)section.ram_addr < 0x80800000 && addr >= (uint32_t)section.ram_addr && addr < (uint32_t)section.ram_addr + section.size) {
                return "in the static code at ROM " + hex32(section.rom_addr) + " (" + hex32((uint32_t)section.ram_addr) + "), offset " + hex32(addr - (uint32_t)section.ram_addr);
            }
        }
        return "not in any loaded code";
    }

    // Native stack trace (function names of the recompiled game when symbols are available)
    std::vector<std::string> native_stack_trace() {
        std::vector<std::string> ret;
#ifdef _WIN32
        void* frames[64];
        USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
        HANDLE process = GetCurrentProcess();
        static bool sym_initialized = false;
        if (!sym_initialized) {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            sym_initialized = SymInitialize(process, nullptr, TRUE);
        }
        alignas(SYMBOL_INFO) char symbol_buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buf);
        for (USHORT i = 0; i < count; i++) {
            DWORD64 addr = (DWORD64)frames[i];
            std::string line;
            HMODULE module = nullptr;
            char module_name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &module)) {
                GetModuleFileNameA(module, module_name, MAX_PATH);
            }
            char buf[512];
            snprintf(buf, sizeof(buf), "%s+0x%llX", std::filesystem::path(module_name).filename().string().c_str(),
                (unsigned long long)(addr - (DWORD64)module));
            line = buf;
            memset(symbol_buf, 0, sizeof(symbol_buf));
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (sym_initialized && SymFromAddr(process, i > 0 ? addr - 1 : addr, &displacement, symbol)) {
                snprintf(buf, sizeof(buf), " %s+0x%llX", symbol->Name, (unsigned long long)(i > 0 ? displacement + 1 : displacement));
                line += buf;
            }
            ret.push_back(line);
        }
#else
        void* frames[64];
        int count = backtrace(frames, 64);
        for (int i = 0; i < count; i++) {
            Dl_info info{};
            char buf[512];
            // Return addresses can be just past the end of a function (calls to functions that don't return)
            void* lookup = i > 0 ? (void*)((char*)frames[i] - 1) : frames[i];
            if (dladdr(lookup, &info) && info.dli_fname != nullptr) {
                snprintf(buf, sizeof(buf), "%s+0x%zX %s+0x%zX",
                    std::filesystem::path(info.dli_fname).filename().c_str(), (size_t)((char*)frames[i] - (char*)info.dli_fbase),
                    info.dli_sname ? info.dli_sname : "?", info.dli_saddr ? (size_t)((char*)frames[i] - (char*)info.dli_saddr) : (size_t)0);
            }
            else {
                snprintf(buf, sizeof(buf), "%p", frames[i]);
            }
            ret.push_back(buf);
        }
#endif
        return ret;
    }

    // State of the game from its memory (z64hdr's gc-eu-mq-dbg addresses)
    std::string game_state() {
        if (rdram_ptr == nullptr || !game_started) {
            return "  (the game wasn't started)\n";
        }
        uint8_t* rdram = rdram_ptr;
        constexpr int32_t save_context = (int32_t)0x8015E660; // gSaveContext
        std::ostringstream out;
        auto field = [&](const char* name, int32_t offset, int size) {
            uint32_t value = 0;
            switch (size) {
                case 4: value = (uint32_t)MEM_W(offset, save_context); break;
                case 2: value = (uint16_t)MEM_H(offset, save_context); break;
                default: value = (uint8_t)MEM_B(offset, save_context); break;
            }
            out << "  " << name << ": " << hex32(value) << "\n";
        };
        field("entranceIndex", 0x0000, 4);
        field("linkAge", 0x0004, 4);
        field("cutsceneIndex", 0x0008, 4);
        field("dayTime", 0x000C, 2);
        field("health", 0x0030, 2);
        field("savedSceneId", 0x0066, 2);
        field("gameMode", 0x135C, 4);
        field("sceneLayer", 0x1360, 4);
        field("respawnFlag", 0x1364, 4);
        return out.str();
    }

    void write_report(const std::string& reason, const std::vector<std::string>& stack) {
        if (report_written.exchange(true)) {
            return;
        }
        fflush(stdout);
        fflush(stderr);
        // Let the capture threads read what was just printed
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::vector<std::string> lines;
        {
            std::lock_guard lock{ output_mutex };
            lines.assign(output_lines.begin(), output_lines.end());
            if (!partial_line.empty()) {
                lines.push_back(partial_line);
            }
        }

        // The error is usually in the last lines of the output
        std::vector<std::string> error_lines;
        static const std::regex error_regex{ "Failed to find function|Switch-case out of bounds|Encountered break|Unhandled|Cannot partially unload|Exiting with exit status|error|Error|ERROR|assert" };
        for (size_t i = lines.size() > 20 ? lines.size() - 20 : 0; i < lines.size(); i++) {
            if (std::regex_search(lines[i], error_regex)) {
                error_lines.push_back(lines[i]);
            }
        }

        std::ostringstream report;
        report << "Sands of Time: Recompiled crash report\n";
        report << "Date: " << timestamp("%Y-%m-%d %H:%M:%S") << "\n";
        report << "Build: " << SOT_BUILD_VERSION << "\n";
#ifdef _WIN32
        report << "OS: Windows\n";
#elif defined(__APPLE__)
        report << "OS: macOS\n";
#else
        report << "OS: Linux\n";
#endif
        report << "\nReason: " << reason << "\n";
        for (const auto& line : error_lines) {
            report << "  " << line << "\n";
        }

        // Describe the game addresses in the error messages
        static const std::regex addr_regex{ "0x(8[0-9A-Fa-f]{7})" };
        std::vector<uint32_t> addrs;
        for (const auto& line : error_lines) {
            for (std::sregex_iterator it{ line.begin(), line.end(), addr_regex }, end; it != end; ++it) {
                uint32_t addr = (uint32_t)std::stoul((*it)[1].str(), nullptr, 16);
                if (std::find(addrs.begin(), addrs.end(), addr) == addrs.end()) {
                    addrs.push_back(addr);
                }
            }
        }
        if (!addrs.empty()) {
            report << "\nAddresses:\n";
            for (uint32_t addr : addrs) {
                report << "  " << hex32(addr) << ": " << describe_address(addr) << "\n";
            }
        }

        report << "\nGame state:\n" << game_state();

        {
            std::lock_guard lock{ overlays_mutex };
            report << "\nLoaded overlays (RAM address, ROM address, size):\n";
            for (const auto& [ram, ovl] : loaded_overlays) {
                report << "  " << hex32(ram) << " " << hex32(ovl.rom) << " " << hex32(ovl.size) << "\n";
            }
        }

        report << "\nStack trace:\n";
        for (const auto& frame : stack) {
            report << "  " << frame << "\n";
        }

        report << "\nConsole output (last " << lines.size() << " lines):\n";
        for (const auto& line : lines) {
            report << "  " << line << "\n";
        }

        std::string text = report.str();
        std::string filename = "crash_" + timestamp("%Y-%m-%d_%H-%M-%S") + ".txt";
        std::filesystem::path written_path;
        for (const auto& dir : { log_dir, fallback_log_dir }) {
            if (dir.empty()) {
                continue;
            }
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::ofstream file{ dir / filename, std::ios::binary };
            if (!file.good()) {
                continue;
            }
            file << text;
            file.close();
            // All the crashes in one file too
            std::ofstream all{ dir / "all_crashes.txt", std::ios::binary | std::ios::app };
            all << text << "\n" << std::string(100, '=') << "\n\n";
            written_path = dir / filename;
            break;
        }

        std::string message = "Sands of Time: Recompiled crashed.\n\n";
        if (!error_lines.empty()) {
            message += error_lines.back() + "\n\n";
        }
        if (!written_path.empty()) {
            message += "A crash log was saved to:\n" + written_path.string();
            fprintf(stderr, "Crash log saved to %s\n", written_path.string().c_str());
        }
        else {
            message += "The crash log couldn't be saved.";
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Sands of Time: Recompiled", message.c_str(), nullptr);
    }

    void on_exit() {
        if (!clean_exit) {
            write_report("the program exited unexpectedly", native_stack_trace());
        }
    }

    void on_terminate() {
        std::string reason = "unhandled C++ exception";
        if (auto ex = std::current_exception()) {
            try {
                std::rethrow_exception(ex);
            }
            catch (const std::exception& e) {
                reason += std::string{ ": " } + e.what();
            }
            catch (...) {
            }
        }
        write_report(reason, native_stack_trace());
        std::_Exit(EXIT_FAILURE);
    }

#ifdef _WIN32
    LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS* info) {
        char reason[128];
        snprintf(reason, sizeof(reason), "native exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
        write_report(reason, native_stack_trace());
        return EXCEPTION_EXECUTE_HANDLER;
    }
#else
    void on_signal(int sig) {
        char reason[64];
        snprintf(reason, sizeof(reason), "signal %d (%s)", sig, strsignal(sig));
        write_report(reason, native_stack_trace());
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }
#endif
}

void sot::crashlog::init(const std::filesystem::path& dir, const std::filesystem::path& fallback_dir) {
    log_dir = dir;
    fallback_log_dir = fallback_dir;
    capture_stream(stdout, captured_stdout);
    capture_stream(stderr, captured_stderr);

    std::atexit(on_exit);
#if !defined(__APPLE__)
    std::at_quick_exit(on_exit);
#endif
    std::set_terminate(on_terminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_unhandled_exception);
    std::signal(SIGABRT, [](int) { write_report("abort", native_stack_trace()); });
#else
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT }) {
        std::signal(sig, on_signal);
    }
#endif
}

void sot::crashlog::set_sections(const SectionTableEntry* section_table, size_t count) {
    sections = section_table;
    num_sections = count;
}

void sot::crashlog::on_game_start(uint8_t* rdram) {
    rdram_ptr = rdram;
    game_started = true;
}

void sot::crashlog::mark_clean_exit() {
    clean_exit = true;
}

extern "C" void sot_on_overlay_load(uint32_t rom, uint32_t ram, uint32_t size) {
    std::lock_guard lock{ overlays_mutex };
    // Overlays loaded over older ones replace them
    for (auto it = loaded_overlays.begin(); it != loaded_overlays.end();) {
        if (it->first < ram + size && ram < it->first + it->second.size) {
            it = loaded_overlays.erase(it);
        }
        else {
            ++it;
        }
    }
    loaded_overlays[ram] = LoadedOverlay{ rom, size };
}

// The game's own error handler (Fault_AddHungupAndCrashImpl): it would show the crash screen and hang
extern "C" void sot_on_game_fault(uint32_t msg1, uint32_t msg2) {
    uint8_t* rdram = rdram_ptr;
    auto read_string = [rdram](uint32_t addr) {
        std::string ret;
        if (rdram == nullptr || addr < 0x80000000 || addr >= 0x80800000) {
            return ret;
        }
        // (signed offsets: the MEM_ macros expect sign-extended addresses)
        for (int32_t i = 0; i < 256; i++) {
            char c = (char)MEM_B(i, (int32_t)addr);
            if (c == '\0') {
                break;
            }
            ret += c;
        }
        return ret;
    };
    std::string reason = "the game reported an error: " + read_string(msg1);
    std::string second = read_string(msg2);
    if (!second.empty()) {
        reason += " / " + second;
    }
    fprintf(stderr, "Game error: %s\n", reason.c_str());
    write_report(reason, native_stack_trace());
    std::_Exit(EXIT_FAILURE);
}
