#include <cstdio>
#include <nuxe/macros.h>

import nuxe;

fn main() -> int { return std::puts("Hello, world!") < 0 ? 1 : 0; }
