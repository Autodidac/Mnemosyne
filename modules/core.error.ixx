module;

#include <cstdint>
#include <expected>
#include <source_location>
#include <string_view>

export module core.error;

export namespace core::error
{
    // Error "domain" lets subsystems own their error space without collisions.
    // 0 is reserved for generic/core errors.
    using domain_t = std::uint32_t;

    struct code
    {
        domain_t domain = 0;
        std::int32_t value = 0;

        constexpr bool operator==(const code&) const noexcept = default;
    };

    struct err
    {
        code c{};
        std::string_view message{};
        std::source_location where = std::source_location::current();

        constexpr explicit operator bool() const noexcept
        {
            return !(c.domain == 0 && c.value == 0);
        }
    };

    template <class T>
    using result = std::expected<T, err>;

    constexpr code ok_code() noexcept { return {}; }
    constexpr err ok() noexcept { return {}; }

    constexpr err make(code c,
                       std::string_view msg = {},
                       std::source_location loc = std::source_location::current()) noexcept
    {
        return {c, msg, loc};
    }

    // Convenience: generic core domain errors.
    namespace core_domain
    {
        inline constexpr domain_t id = 0;

        inline constexpr std::int32_t failed = 1;
        inline constexpr std::int32_t invalid_argument = 2;
        inline constexpr std::int32_t not_found = 3;
        inline constexpr std::int32_t io_error = 4;
        inline constexpr std::int32_t unsupported = 5;
    }

    constexpr err failed(std::string_view msg = {},
                         std::source_location loc = std::source_location::current()) noexcept
    {
        return make({core_domain::id, core_domain::failed}, msg, loc);
    }

    constexpr err invalid_argument(std::string_view msg = {},
                                   std::source_location loc = std::source_location::current()) noexcept
    {
        return make({core_domain::id, core_domain::invalid_argument}, msg, loc);
    }
}
