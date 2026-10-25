#pragma once

#include "renderer/feature.hpp"

#include <string>
#include <utility>
#include <vector>

namespace renderlab::renderer
{
class OpaqueFeature final : public RenderFeature
{
public:
    OpaqueFeature() : RenderFeature("Forward.Opaque") {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};
class TransparentFeature final : public RenderFeature
{
public:
    TransparentFeature() : RenderFeature("Forward.Transparent", {"Forward.Opaque"}) {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};
/** @brief A physical color copy; destination remains independent of later source writes. */
class ColorSnapshotFeature final : public RenderFeature
{
public:
    ColorSnapshotFeature(std::string source, std::string destination,
                         std::vector<std::string> dependencies = {})
        : RenderFeature(destination, std::move(dependencies)), source_(std::move(source)),
          destination_(std::move(destination))
    {
    }
    void Build(rendergraph::Graph&, const RenderFrame&) override;

private:
    std::string source_, destination_;
};
} // namespace renderlab::renderer
