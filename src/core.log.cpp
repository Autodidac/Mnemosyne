module;

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  include <windows.h>
#endif

module core.log;

namespace core::log
{
    namespace
    {
        std::mutex g_mtx;
        level g_min = level::info;
        bool g_console = true;
        bool g_debugger = false;
        std::FILE* g_file = nullptr;

        constexpr std::string_view lvl_text(level lvl) noexcept
        {
            switch (lvl)
            {
            case level::trace: return "trace";
            case level::info:  return "info";
            case level::warn:  return "warn";
            case level::error: return "error";
            case level::off:   return "off";
            }
            return "unknown";
        }

        std::uint64_t now_ms() noexcept
        {
            using clock = std::chrono::system_clock;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                clock::now().time_since_epoch()
            ).count();
            return static_cast<std::uint64_t>(ms);
        }

#if defined(_WIN32)
        std::wstring utf8_to_wide(std::string_view s)
        {
            if (s.empty()) return {};
            const int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (wlen <= 0) return {};
            std::wstring w;
            w.resize(static_cast<std::size_t>(wlen));
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), wlen);
            return w;
        }
#endif

        void sink_write(std::string_view line)
        {
            if (g_console)
            {
                std::println("{}", line);
            }

            if (g_file)
            {
                std::fwrite(line.data(), 1, line.size(), g_file);
                std::fwrite("\n", 1, 1, g_file);
                std::fflush(g_file);
            }

#if defined(_WIN32)
            if (g_debugger)
            {
                std::string tmp(line);
                tmp.push_back('\n');
                OutputDebugStringA(tmp.c_str());
            }
#else
            (void)line;
#endif
        }

        bool enabled(level lvl) noexcept
        {
            return (g_min != level::off) &&
                (static_cast<unsigned>(lvl) >= static_cast<unsigned>(g_min));
        }
    }

    void set_level(level min_level) noexcept
    {
        std::lock_guard lk(g_mtx);
        g_min = min_level;
    }

    level get_level() noexcept
    {
        std::lock_guard lk(g_mtx);
        return g_min;
    }

    void enable_console(bool on) noexcept
    {
        std::lock_guard lk(g_mtx);
        g_console = on;
    }

    void enable_debugger(bool on) noexcept
    {
        std::lock_guard lk(g_mtx);
#if defined(_WIN32)
        g_debugger = on;
#else
        (void)on;
        g_debugger = false;
#endif
    }

    bool set_file(std::string_view utf8_path) noexcept
    {
        std::lock_guard lk(g_mtx);

        if (g_file)
        {
            std::fclose(g_file);
            g_file = nullptr;
        }

#if defined(_WIN32)
        const std::wstring wpath = utf8_to_wide(utf8_path);
        if (wpath.empty())
            return false;

        FILE* f = nullptr;
        if (_wfopen_s(&f, wpath.c_str(), L"ab") != 0)
            return false;

        g_file = f;
        return true;
#else
        std::string p{ utf8_path };
        g_file = std::fopen(p.c_str(), "ab");
        return g_file != nullptr;
#endif
    }

    void close_file() noexcept
    {
        std::lock_guard lk(g_mtx);
        if (g_file)
        {
            std::fclose(g_file);
            g_file = nullptr;
        }
    }

    void write(level lvl, std::string_view tag, std::string_view msg)
    {
        std::lock_guard lk(g_mtx);
        if (!enabled(lvl)) return;

        const auto t = now_ms();
        const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());

        std::string line;
        line.reserve(tag.size() + msg.size() + 64);
        line.append("[");
        line.append(std::to_string(t));
        line.append("][");
        line.append(lvl_text(lvl));
        line.append("][");
        line.append(std::to_string(static_cast<unsigned long long>(tid)));
        line.append("][");
        line.append(tag);
        line.append("] ");
        line.append(msg);

        sink_write(line);
    }

    void write_kv(level lvl, std::string_view tag, std::string_view msg, std::span<const kv> fields)
    {
        std::lock_guard lk(g_mtx);
        if (!enabled(lvl)) return;

        const auto t = now_ms();
        const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());

        std::string line;
        line.reserve(tag.size() + msg.size() + fields.size() * 16 + 96);
        line.append("[");
        line.append(std::to_string(t));
        line.append("][");
        line.append(lvl_text(lvl));
        line.append("][");
        line.append(std::to_string(static_cast<unsigned long long>(tid)));
        line.append("][");
        line.append(tag);
        line.append("] ");
        line.append(msg);

        for (const auto& [k, v] : fields)
        {
            line.push_back(' ');
            line.append(k);
            line.push_back('=');
            line.append(v);
        }

        sink_write(line);
    }
}

// C ABI bridge for non-module TUs (e.g., App project) to log without importing modules.
extern "C" void core_log_write(std::uint32_t lvl, const char* tag_utf8, const char* msg_utf8)
{
    using core::log::level;

    auto clamp_level = [](std::uint32_t v) -> level
        {
            if (v > static_cast<std::uint32_t>(level::off))
                return level::off;
            return static_cast<level>(v);
        };

    const auto L = clamp_level(lvl);
    const std::string_view tag = tag_utf8 ? std::string_view{ tag_utf8 } : std::string_view{};
    const std::string_view msg = msg_utf8 ? std::string_view{ msg_utf8 } : std::string_view{};
    core::log::write(L, tag, msg);
}
