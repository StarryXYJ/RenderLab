#pragma once

#include "platform/window.hpp"
#include "rhi/cpu_profile.hpp"
#include "rhi/resources.hpp"
#include "rhi/swapchain.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace renderlab::rhi
{
class CommandList;
class RecordingWorkers;

/** @brief One graphics queue. Monotonic completion points represent actual successful submissions.
 */
class Runtime
{
public:
    virtual ~Runtime();
    virtual std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
    /// Immutable default or sampled view; repeated calls may share a still-live view.
    virtual std::shared_ptr<TextureView> CreateView(std::shared_ptr<Texture> texture,
                                                    TextureViewUsage usage) = 0;
    std::shared_ptr<TextureView> CreateView(std::shared_ptr<Texture> texture)
    {
        return CreateView(std::move(texture), TextureViewUsage::Default);
    }
    virtual std::shared_ptr<Sampler> CreateSampler() = 0;
    virtual std::shared_ptr<BindingLayout> CreateLayout(std::vector<BindingDesc> entries) = 0;
    virtual std::shared_ptr<BindingGroup> CreateGroup(std::shared_ptr<BindingLayout> layout,
                                                      std::vector<Binding> bindings) = 0;
    /// @brief Query native BLAS/TLAS storage and build scratch requirements.
    AccelerationStructureSizes
    QueryAccelerationStructureSizes(const AccelerationStructureDesc& desc);
    /// @brief Allocate a versioned acceleration structure and its private scratch/input storage.
    std::shared_ptr<AccelerationStructure>
    CreateAccelerationStructure(const AccelerationStructureDesc& desc);
    /// Unified bounded weak cache; immutable shader/layout identities and every PSO state form the
    /// key.
    std::shared_ptr<Pipeline> CreatePipeline(PipelineDesc desc);
    /// Rebuild for every pipeline generation; records contain no material-specific local data.
    std::shared_ptr<ShaderTable> CreateShaderTable(std::shared_ptr<Pipeline> pipeline);
    /// vsync=false requests unsynchronized presentation; unsupported modes fail explicitly.
    virtual std::unique_ptr<Swapchain> CreateSwapchain(platform::NativeSurface surface,
                                                       uint32_t width, uint32_t height,
                                                       bool vsync = true) = 0;
    std::shared_ptr<ShaderModule> CreateShader(ShaderStage stage, std::vector<std::byte> code,
                                               std::string entry);
    std::unique_ptr<CommandList> CreateCommands();
    uint64_t Submit(std::unique_ptr<CommandList> commands, Swapchain* presentation = nullptr);
    void Collect();
    void Wait(uint64_t point);
    void WaitIdle();
    virtual uint64_t Completed() const = 0;
    virtual double TimestampPeriodNs() const = 0;
    /// Enabled features/limits. Nonzero firstInstance in GPU arguments requires firstInstance.
    virtual IndirectSupport GetIndirectSupport() const
    {
        return {};
    }
    /// Bounded, immutable sampled-image arrays with nonuniform indexing; zero if unsupported.
    virtual uint32_t SampledTextureArrayLimit() const
    {
        return 0;
    }
    virtual bool SupportsAccelerationStructures() const
    {
        return false;
    }
    virtual bool SupportsRayQuery() const
    {
        return false;
    }
    virtual bool SupportsRayTracingPipeline() const
    {
        return false;
    }
    MemoryStats GetMemoryStats() const
    {
        return ledger_->stats;
    }
    /// Owner-thread application limit for tracked native allocations; does not reserve VRAM.
    /// Rejects zero or a limit below current usage. Configure before creating renderer pools.
    void SetAllocationBudget(uint64_t bytes);
    DescriptorStats GetDescriptorStats() const
    {
        return *descriptorStats_;
    }
    size_t PendingCount() const
    {
        return pending_.size();
    }
    uint64_t LastSubmitted() const
    {
        return lastSubmitted_;
    }
    /// Owner-thread diagnostic scopes; enable between calls. Cumulative counters never reset.
    void SetCpuProfiling(bool enabled)
    {
        cpuProfiling_ = enabled;
    }
    CpuProfileSnapshot GetCpuProfile() const
    {
        return cpuProfile_;
    }

protected:
    Runtime();
    void RecordNative(size_t count, const std::function<void(size_t)>& job);
    bool CpuProfiling() const
    {
        return cpuProfiling_;
    }
    void AddCpuMeasurement(CpuOperation operation, CpuMeasurement measurement);
    CpuTimer ProfileCpu(CpuOperation operation)
    {
        return CpuTimer(cpuProfiling_ ? &cpuProfile_.at(size_t(operation)) : nullptr);
    }
    virtual std::shared_ptr<Pipeline> CreatePipelineNative(PipelineDesc desc) = 0;
    virtual std::shared_ptr<ShaderTable> CreateShaderTableNative(std::shared_ptr<Pipeline>);
    virtual AccelerationStructureSizes
    QueryAccelerationStructureSizesNative(const AccelerationStructureDesc& desc);
    virtual std::shared_ptr<AccelerationStructure>
    CreateAccelerationStructureNative(AccelerationStructureDesc desc,
                                      AccelerationStructureSizes sizes);
    struct Submission
    {
        virtual ~Submission() = default;
    };
    virtual std::shared_ptr<Submission> Execute(const CommandList& commands, uint64_t point,
                                                Swapchain* presentation) = 0;
    virtual void WaitNative(uint64_t point) = 0;
    void Validate(const BufferDesc& desc) const;
    void ValidateLayout(const std::vector<BindingDesc>& entries) const;
    void Validate(const TextureDesc& desc) const;
    void Validate(const AccelerationStructureDesc& desc) const;
    void Validate(const PipelineDesc& desc) const;
    void Validate(const std::shared_ptr<BindingLayout>& layout,
                  const std::vector<Binding>& bindings) const;
    void CheckOwner(const std::shared_ptr<Object>& object) const;
    std::shared_ptr<AllocationLedger> ledger_ = std::make_shared<AllocationLedger>();
    std::shared_ptr<DescriptorStats> descriptorStats_ = std::make_shared<DescriptorStats>();

private:
    std::unique_ptr<RecordingWorkers> recordingWorkers_;
    bool cpuProfiling_ = false;
    CpuProfileSnapshot cpuProfile_{};
    struct Pending
    {
        uint64_t point;
        std::unique_ptr<CommandList> commands;
        std::shared_ptr<Submission> native;
    };
    std::deque<Pending> pending_;
    uint64_t lastSubmitted_ = 0;
    std::thread::id ownerThread_ = std::this_thread::get_id();
    std::vector<std::weak_ptr<Pipeline>> pipelines_;
};
} // namespace renderlab::rhi
