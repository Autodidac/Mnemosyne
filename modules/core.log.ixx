module;

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

export module core.log;

export namespace core::log
{
    enum class level : std::uint32_t
    {
        trace = 0,
        info = 1,
        warn = 2,
        error = 3,
        off = 4,
    };

    using kv = std::pair<std::string_view, std::string_view>;

    // Configuration (process-global, but explicit).
    void set_level(level min_level) noexcept;
    level get_level() noexcept;

    void enable_console(bool on) noexcept;   // stdout
    void enable_debugger(bool on) noexcept;  // OutputDebugString on Windows, no-op elsewhere
    bool set_file(std::string_view utf8_path) noexcept; // append mode (UTF-8 path)
    void close_file() noexcept;

    // Log entry points.
    void write(level lvl, std::string_view tag, std::string_view msg);
    void write_kv(level lvl, std::string_view tag, std::string_view msg, std::span<const kv> fields);

    inline void trace(std::string_view tag, std::string_view msg) { write(level::trace, tag, msg); }
    inline void info(std::string_view tag, std::string_view msg) { write(level::info, tag, msg); }
    inline void warn(std::string_view tag, std::string_view msg) { write(level::warn, tag, msg); }
    inline void error(std::string_view tag, std::string_view msg) { write(level::error, tag, msg); }

    // C ABI bridge for non-module translation units (App project, tools, etc.)
    // Implemented in src/core.log.cpp.
    extern "C" void core_log_write(std::uint32_t lvl, const char* tag_utf8, const char* msg_utf8);
}
