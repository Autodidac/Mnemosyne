#include <cstdlib>

import core.assert;
import core.env;
import core.error;
import core.format;
import core.id;
import core.math;
import core.path;
import core.string;
import core.time;

int main()
{
    using core::asserts::that;

    // core.math
    that(core::math::clamp(5, 0, 3) == 3);
    that(core::math::lerp(0.0, 10.0, 0.5) == 5.0);

    // core.string
    that(core::string::trim("  hi  ") == "hi");

    // core.id (basic compile-time sanity)
    struct TagA {};
    core::id::strong_id<TagA> a{42};
    that(a.value == 42);

    // core.env
    (void)core::env::set("DEMO_TEST_ENV", "123");
    auto v = core::env::get("DEMO_TEST_ENV");
    that(v.has_value() && *v == "123");
    (void)core::env::unset("DEMO_TEST_ENV");

    // core.path (best-effort existence)
    auto exe = core::path::executable_path();
    that(!exe.empty());

    // core.error (source_location present)
    auto e = core::error::failed("x");
    that((bool)e);

    return 0;
}
