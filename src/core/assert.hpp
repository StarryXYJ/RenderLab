#pragma once

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace renderlab
{
[[noreturn]] inline void
AssertionFailed(const char* expression,
                const std::source_location location = std::source_location::current())
{
    std::fprintf(stderr, "Assertion failed: %s (%s:%u)\n", expression, location.file_name(),
                 location.line());
    std::abort();
}
} // namespace renderlab

#ifndef NDEBUG
#define RENDERLAB_ASSERT(expression)                                                               \
    ((expression) ? static_cast<void>(0) : ::renderlab::AssertionFailed(#expression))
#else
#define RENDERLAB_ASSERT(expression) static_cast<void>(0)
#endif
