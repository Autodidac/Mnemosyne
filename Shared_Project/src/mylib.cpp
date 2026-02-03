module;

#include <numeric>
#include <span>
#include <vector>

module mylib;

namespace mylib
{
    std::vector<int> map_mul(std::span<const int> xs, int k)
    {
        std::vector<int> out;
        out.reserve(xs.size());
        for (int v : xs) out.push_back(v * k);
        return out;
    }

    int reduce_sum(std::span<const int> xs) noexcept
    {
        return std::accumulate(xs.begin(), xs.end(), 0);
    }

    int entry() noexcept
    {
        const std::vector<int> base{1, 2, 3, 4, 5};
        const auto doubled = map_mul(base, 2);
        return reduce_sum(doubled);
    }
}
