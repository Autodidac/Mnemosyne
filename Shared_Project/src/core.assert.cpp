module;

#include <source_location>
#include <string>
#include <string_view>
#include <cstdlib>

#if defined(_WIN32)
#  include <windows.h>
#endif

module core.assert;

import core.log;

namespace core::asserts
{
    namespace
    {
#if defined(NDEBUG)
        inline constexpr bool kDebugEnabled = false;
#else
        inline constexpr bool kDebugEnabled = true;
#endif

        [[noreturn]] void fail(std::string_view kind,
            std::string_view message,
            const std::source_location& where)
        {
            // Single coherent line: easier to grep, easier to parse.
            std::string line;
            line.reserve(256);

            line.append(std::string_view{ "[" });
            line.append(kind);
            line.append(std::string_view{ "] " });
            line.append(message.empty() ? std::string_view{ "<no message>" } : message);
            line.append(std::string_view{ " @ " });
            line.append(where.file_name());
            line.push_back(':');
            line.append(std::to_string(where.line()));
            line.append(std::string_view{ " (" });
            line.append(where.function_name());
            line.push_back(')');

            core::log::error("assert", line);

#if defined(_WIN32)
            // Fail-fast + debugger friendliness.
            ::DebugBreak();
#endif
            std::terminate();
        }
    }

    void that(bool condition, std::string_view message, std::source_location where)
    {
        if (!condition)
            fail("assert", message, where);
    }

    void debug(bool condition, std::string_view message, std::source_location where)
    {
        if constexpr (kDebugEnabled)
        {
            if (!condition)
                fail("debug_assert", message, where);
        }
        else
        {
            (void)condition; (void)message; (void)where;
        }
    }
}
