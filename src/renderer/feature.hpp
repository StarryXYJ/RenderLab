#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace renderlab::rendergraph
{
class Graph;
}
namespace renderlab::renderer
{
struct RenderFrame;
/** @brief Persistent feature configuration. Dependencies name feature instances, not GPU resources.
 * Build must not retain graph handles or frame references; callbacks own their captured inputs. */
class RenderFeature
{
public:
    RenderFeature(std::string name, std::vector<std::string> dependencies = {})
        : name_(std::move(name)), dependencies_(std::move(dependencies))
    {
        if (name_.empty())
            throw std::invalid_argument("Empty render feature name");
    }
    virtual ~RenderFeature() = default;
    const std::string& Name() const
    {
        return name_;
    }
    const std::vector<std::string>& Dependencies() const
    {
        return dependencies_;
    }
    virtual void Build(rendergraph::Graph& graph, const RenderFrame& frame) = 0;

private:
    const std::string name_;
    const std::vector<std::string> dependencies_;
};
} // namespace renderlab::renderer
