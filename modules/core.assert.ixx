module;

#include <source_location>
#include <string_view>

export module core.assert;

export namespace core::asserts
{
    // Always-on check. Fails fast (logs then terminates).
    void that(bool condition,
        std::string_view message = {},
        std::source_location where = std::source_location::current());

    // Debug-only check: compiled out when NDEBUG is defined.
    void debug(bool condition,
        std::string_view message = {},
        std::source_location where = std::source_location::current());
}
