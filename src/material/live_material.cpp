#include "material/live_material.hpp"

#include <stdexcept>

namespace renderlab::material
{
LiveMaterial::LiveMaterial(std::shared_ptr<const shader::Package> package, Asset asset)
    : active_{package, Migrate(asset, shader::MaterialTemplate(package)), {}}
{
}
void LiveMaterial::Stage(Runtime& runtime, std::shared_ptr<const shader::Package> package,
                         std::span<const Preparation> preparations, const ResourceResolver& resolve)
{
    Stage(runtime, std::move(package), active_.asset, preparations, resolve);
}
void LiveMaterial::Stage(Runtime& runtime, std::shared_ptr<const shader::Package> package,
                         const Asset& asset, std::span<const Preparation> preparations,
                         const ResourceResolver& resolve)
{
    if (!package || package->generation < active_.package->generation ||
        (staged_ && package->generation < staged_->package->generation))
        throw std::invalid_argument("Cannot stage a stale shader generation");
    if (preparations.empty())
        throw std::invalid_argument("Reload needs required pass preparations");
    State next{package, Migrate(asset, shader::MaterialTemplate(package)), {}};
    for (const auto& preparation : preparations)
        next.snapshots.push_back(runtime.Prepare(package, next.asset, preparation, resolve));
    staged_ = std::move(next);
}
bool LiveMaterial::PublishFrameBoundary()
{
    if (!staged_)
        return false;
    active_ = std::move(*staged_);
    staged_.reset();
    return true;
}
} // namespace renderlab::material
