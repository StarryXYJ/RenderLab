#pragma once

#include "material/runtime.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace renderlab::material
{
/** @brief Transactional generation replacement. Stage prepares every required pass before publish.
 */
class LiveMaterial final
{
public:
    LiveMaterial(std::shared_ptr<const shader::Package> package, Asset asset);
    /// Failure leaves active and any already staged generation intact; caller may retry.
    void Stage(Runtime& runtime, std::shared_ptr<const shader::Package> package,
               std::span<const Preparation> preparations, const ResourceResolver& resolve = {});
    /// Stage edited logical values using the same transaction as shader replacement.
    void Stage(Runtime& runtime, std::shared_ptr<const shader::Package> package, const Asset& asset,
               std::span<const Preparation> preparations, const ResourceResolver& resolve = {});
    bool PublishFrameBoundary();
    const Asset& GetAsset() const
    {
        return active_.asset;
    }
    const std::shared_ptr<const shader::Package>& GetPackage() const
    {
        return active_.package;
    }
    const std::vector<Prepared>& Snapshots() const
    {
        return active_.snapshots;
    }

private:
    struct State
    {
        std::shared_ptr<const shader::Package> package;
        Asset asset;
        std::vector<Prepared> snapshots;
    };
    State active_;
    std::optional<State> staged_;
};
} // namespace renderlab::material
