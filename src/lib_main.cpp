#include "main.h"

import runtime;

// Must be global + C linkage so the symbol name is exactly: internal_entry_anchor
extern "C" void internal_entry_anchor() noexcept {}

int main()
{
    return runtime::run();
}
