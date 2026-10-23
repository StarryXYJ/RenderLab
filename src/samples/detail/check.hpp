#pragma once

#include <stdexcept>
#include <string>

namespace renderlab::samples::detail
{
inline void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("M1 acceptance: " + message);
}
} // namespace renderlab::samples::detail
