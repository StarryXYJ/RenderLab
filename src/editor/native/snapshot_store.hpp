#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace renderlab::editor
{
/** @brief CPU value snapshots only. The session mutex protects all operations. */
struct SnapshotStore
{
    static constexpr uint32_t Budget = 4 * 1024 * 1024;
    std::string latest, pinned;
    uint64_t sequence = 0, pin = 0;
    bool Publish(std::string value)
    {
        if (value.size() + 1 > Budget)
            return false;
        latest = std::move(value);
        return true;
    }
    std::pair<uint64_t, uint32_t> Acquire()
    {
        if (pin != 0)
            return {pin, static_cast<uint32_t>(pinned.size() + 1)};
        if (latest.empty())
            return {};
        pinned = latest;
        pin = ++sequence;
        return {pin, static_cast<uint32_t>(pinned.size() + 1)};
    }
    bool Copy(uint64_t id, char* output, uint32_t capacity) const
    {
        if (id != pin || !id || !output || capacity < pinned.size() + 1)
            return false;
        std::memcpy(output, pinned.c_str(), pinned.size() + 1);
        return true;
    }
    bool Release(uint64_t id)
    {
        if (id != pin || !id)
            return false;
        pinned.clear();
        pin = 0;
        return true;
    }
};
} // namespace renderlab::editor
