#include "samples/common/demo_geometry.hpp"
#include "rhi/commands.hpp"
#include "rhi/upload.hpp"

#include <array>
#include <span>

namespace renderlab::samples::detail
{
using namespace rhi;
namespace
{
template <class T> auto Bytes(const T& value)
{
    return std::as_bytes(std::span{&value, size_t{1}});
}
} // namespace
DemoGeometry CreateDemoGeometry(rhi::Runtime& runtime)
{
    DemoGeometry result;
    const std::array<DemoVertex, 18> vertices{
        {{{-.9f, -.65f, .75f}, {1, 0, 0, 1}, {0, 0}},
         {{-.1f, -.65f, .75f}, {0, 1, 0, 1}, {0, 0}},
         {{-.5f, .65f, .75f}, {0, 0, 1, 1}, {0, 0}},
         {{.05f, -.75f, .5f}, {0, 0, 0, 0}, {0, 1}},
         {{.9f, -.75f, .5f}, {0, 0, 0, 0}, {1, 1}},
         {{.9f, .75f, .5f}, {0, 0, 0, 0}, {1, 0}},
         {{.05f, -.75f, .5f}, {0, 0, 0, 0}, {0, 1}},
         {{.9f, .75f, .5f}, {0, 0, 0, 0}, {1, 0}},
         {{.05f, .75f, .5f}, {0, 0, 0, 0}, {0, 0}},
         {{-.9f, -.65f, .25f}, {1, 0, 1, 1}, {0, 0}},
         {{-.1f, -.65f, .25f}, {1, 0, 1, 1}, {0, 0}},
         {{-.5f, .65f, .25f}, {1, 0, 1, 1}, {0, 0}},
         {{-.9f, .75f, .5f}, {.25f, .25f, .25f, .5f}, {0, 0}},
         {{-.1f, .75f, .5f}, {.25f, .25f, .25f, .5f}, {0, 0}},
         {{-.1f, .9f, .5f}, {.25f, .25f, .25f, .5f}, {0, 0}},
         {{-.9f, .75f, .5f}, {.25f, .25f, .25f, .5f}, {0, 0}},
         {{-.1f, .9f, .5f}, {.25f, .25f, .25f, .5f}, {0, 0}},
         {{-.9f, .9f, .5f}, {.25f, .25f, .25f, .5f}, {0, 0}}}};
    const std::array<uint32_t, 3> indices{0, 1, 2};
    const DrawArguments indirect{3, 1, 9, 0};
    UploadArena upload(runtime, 4096);
    auto commands = runtime.CreateCommands();
    const auto uploadBuffer =
        [&](std::span<const std::byte> bytes, State finalState, const char* name)
    {
        auto buffer = runtime.CreateBuffer({bytes.size(), Memory::Device, name});
        auto slice = upload.Write(bytes);
        commands->Barrier(slice.buffer, State::HostWrite, State::CopySource);
        commands->Barrier(buffer, State::Undefined, State::CopyDest);
        commands->Copy(slice.buffer, buffer, bytes.size(), slice.offset);
        commands->Barrier(buffer, State::CopyDest, finalState);
        return buffer;
    };
    result.vertices = uploadBuffer(Bytes(vertices), State::Vertex, "Demo.Vertices");
    result.indices = uploadBuffer(Bytes(indices), State::Index, "Demo.Indices");
    result.indirect = uploadBuffer(Bytes(indirect), State::Indirect, "Demo.IndirectArguments");
    runtime.Wait(runtime.Submit(std::move(commands)));
    return result;
}
} // namespace renderlab::samples::detail
