#pragma once

#include "core/log.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace renderlab::rhi
{
enum class Backend
{
    Dx12,
    Vulkan
};

enum class ValidationMode
{
    Auto,
    On,
    Off
};

struct DeviceOptions
{
    Backend backend = Backend::Dx12;
    ValidationMode validation = ValidationMode::Auto;
    std::optional<uint32_t> adapterIndex;
};

struct AdapterInfo
{
    uint32_t index = 0;
    std::string name;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    /// API-reported local memory, possibly shared on integrated GPUs; not a live memory budget.
    uint64_t deviceLocalMemory = 0;
    bool software = false;
};

struct QueueInfo
{
    std::string name;
    uint32_t nativeIndex = 0;
    uint32_t count = 0;
    bool graphics = false;
    bool compute = false;
    bool transfer = false;
    bool selected = false;
};

struct FormatSupport
{
    std::string name;
    bool sampled = false;
    bool storage = false;
    bool colorAttachment = false;
    bool depthAttachment = false;
};

/**
 * @brief Hardware capabilities, independent of enabled device features and implemented render
 * paths.
 */
struct DeviceCapabilities
{
    bool indirectDraw = false;
    bool multiDrawIndirect = false;
    bool indirectCount = false;
    bool indirectFirstInstance = false;
    bool descriptorIndexing = false;
    bool accelerationStructure = false;
    bool rayQuery = false;
    bool rayTracingPipeline = false;
    bool meshShader = false;
    uint32_t maxTexture2D = 0;
    uint32_t maxColorAttachments = 0;
};

/** @brief Owned capability snapshot that remains readable after native device shutdown. */
struct DeviceReport
{
    std::string backend;
    std::string apiVersion;
    std::string driverVersion;
    std::string shaderTarget;
    bool validationEnabled = false;
    uint32_t selectedAdapter = 0;
    std::vector<AdapterInfo> adapters;
    std::vector<QueueInfo> queues;
    std::vector<FormatSupport> formats;
    DeviceCapabilities supported;
    std::vector<std::pair<std::string, std::string>> details;
    std::vector<std::string> warnings;
};

/**
 * @brief Owns a backend device and its initialization diagnostics.
 *
 * M0 creates a device and queue without submitting GPU work. The logger supplied to CreateDevice
 * must outlive this object, including initialization failure and shutdown callbacks.
 */
class Device
{
public:
    virtual ~Device() = default;
    virtual const DeviceReport& GetReport() const = 0;
    /// Collect pending diagnostics; callback-driven backends may have already recorded them.
    virtual void PollDiagnostics() = 0;
    /**
     * @brief Releases native objects idempotently, retaining the report and diagnostic counters.
     * @pre No outstanding GPU work may reference these objects; M0 submits no GPU work.
     */
    virtual void Shutdown() noexcept = 0;
    /// Read after Shutdown to include diagnostics emitted during cleanup.
    virtual uint32_t GetValidationErrorCount() const = 0;
};

/**
 * @brief Creates the requested backend and checks the minimum device and format requirements.
 * @param logger Receives diagnostics and must outlive the returned device.
 * @throws std::runtime_error If initialization or a required capability check fails.
 */
std::unique_ptr<Device> CreateDevice(const DeviceOptions& options, Logger& logger);
std::unique_ptr<Device> CreateDx12Device(const DeviceOptions& options, Logger& logger);
std::unique_ptr<Device> CreateVulkanDevice(const DeviceOptions& options, Logger& logger);
/// Serialize an owned report as UTF-8 JSON without querying the native device.
std::string ReportToJson(const DeviceReport& report, uint32_t validationErrors);
void ValidateRequiredFormats(const DeviceReport& report);
} // namespace renderlab::rhi
