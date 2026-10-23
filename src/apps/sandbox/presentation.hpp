#pragma once

#include <filesystem>

namespace renderlab
{
class Logger;
namespace platform
{
class Window;
}
namespace rhi
{
class Device;
class Runtime;
} // namespace rhi
namespace sandbox
{
struct Options;
/// Own the presentation loop and frame contexts; the window/device/runtime remain host-owned.
void RunRendering(platform::Window* window, rhi::Device& device, rhi::Runtime& runtime,
                  const Options& options, const std::filesystem::path& shaderRoot, Logger& logger);
} // namespace sandbox
} // namespace renderlab
