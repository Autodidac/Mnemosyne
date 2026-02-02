module;

#include <span>
#include <vector>

export module mylib;

export namespace mylib
{
    // "internal entrypoint" you can call from anywhere (including lib main).
    int entry() noexcept;

    // functional-ish helpers
    std::vector<int> map_mul(std::span<const int> xs, int k);
    int reduce_sum(std::span<const int> xs) noexcept;
}
