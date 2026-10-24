#pragma once

namespace renderlab::rhi
{
class Runtime;
}

namespace renderlab::samples
{
/// Transfer, recording lifetime and bounded upload reuse acceptance checks.
void CheckResourceContracts(rhi::Runtime& runtime);
} // namespace renderlab::samples
