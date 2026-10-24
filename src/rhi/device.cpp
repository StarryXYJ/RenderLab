#include "rhi/device.hpp"

#include <format>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace renderlab::rhi
{
namespace
{
std::string Quote(std::string_view text)
{
    std::string result = "\"";
    for (const unsigned char value : text)
    {
        switch (value)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (value < 0x20)
            {
                result += std::format("\\u{:04x}", value);
            }
            else
            {
                result += static_cast<char>(value);
            }
        }
    }
    return result + '"';
}
} // namespace

std::unique_ptr<Device> CreateDevice(const DeviceOptions& options, Logger& logger)
{
    auto device = options.backend == Backend::Dx12 ? CreateDx12Device(options, logger)
                                                   : CreateVulkanDevice(options, logger);
    ValidateRequiredFormats(device->GetReport());
    return device;
}

void ValidateRequiredFormats(const DeviceReport& report)
{
    for (const auto* name : {"RGBA8_UNORM", "RGBA16_FLOAT", "D32_FLOAT"})
    {
        bool supported = false;
        for (const auto& format : report.formats)
        {
            if (format.name == name)
            {
                supported = std::string_view(name) == "D32_FLOAT"
                                ? format.depthAttachment
                                : format.colorAttachment && format.sampled;
                break;
            }
        }
        if (!supported)
        {
            throw std::runtime_error(std::format("Required format {} is unsupported", name));
        }
    }
}

std::string ReportToJson(const DeviceReport& report, uint32_t validationErrors)
{
    std::ostringstream out;
    out << std::boolalpha << "{\n  \"schemaVersion\": 1,\n  \"backend\": " << Quote(report.backend)
        << ",\n  \"apiVersion\": " << Quote(report.apiVersion)
        << ",\n  \"driverVersion\": " << Quote(report.driverVersion)
        << ",\n  \"shaderTarget\": " << Quote(report.shaderTarget)
        << ",\n  \"validationEnabled\": " << report.validationEnabled
        << ",\n  \"validationErrors\": " << validationErrors
        << ",\n  \"selectedAdapter\": " << report.selectedAdapter << ",\n  \"adapters\": [";
    const char* separator = "";
    for (const auto& adapter : report.adapters)
    {
        out << separator << "\n    {\"index\": " << adapter.index
            << ", \"name\": " << Quote(adapter.name) << ", \"vendorId\": " << adapter.vendorId
            << ", \"deviceId\": " << adapter.deviceId
            << ", \"deviceLocalMemoryBytes\": " << adapter.deviceLocalMemory
            << ", \"software\": " << adapter.software << '}';
        separator = ",";
    }
    out << "\n  ],\n  \"queues\": [";
    separator = "";
    for (const auto& queue : report.queues)
    {
        out << separator << "\n    {\"name\": " << Quote(queue.name)
            << ", \"nativeIndex\": " << queue.nativeIndex << ", \"count\": " << queue.count
            << ", \"graphics\": " << queue.graphics << ", \"compute\": " << queue.compute
            << ", \"transfer\": " << queue.transfer << ", \"selected\": " << queue.selected << '}';
        separator = ",";
    }
    out << "\n  ],\n  \"formats\": [";
    separator = "";
    for (const auto& format : report.formats)
    {
        out << separator << "\n    {\"name\": " << Quote(format.name)
            << ", \"sampled\": " << format.sampled << ", \"storage\": " << format.storage
            << ", \"colorAttachment\": " << format.colorAttachment
            << ", \"depthAttachment\": " << format.depthAttachment << '}';
        separator = ",";
    }
    const auto& caps = report.supported;
    out << "\n  ],\n  \"supported\": {\n"
        << "    \"indirectDraw\": " << caps.indirectDraw
        << ",\n    \"multiDrawIndirect\": " << caps.multiDrawIndirect
        << ",\n    \"indirectCount\": " << caps.indirectCount
        << ",\n    \"indirectFirstInstance\": " << caps.indirectFirstInstance
        << ",\n    \"descriptorIndexing\": " << caps.descriptorIndexing
        << ",\n    \"accelerationStructure\": " << caps.accelerationStructure
        << ",\n    \"rayQuery\": " << caps.rayQuery
        << ",\n    \"rayTracingPipeline\": " << caps.rayTracingPipeline
        << ",\n    \"meshShader\": " << caps.meshShader
        << ",\n    \"maxTexture2D\": " << caps.maxTexture2D
        << ",\n    \"maxColorAttachments\": " << caps.maxColorAttachments
        << "\n  },\n  \"details\": {";
    separator = "";
    for (const auto& [key, value] : report.details)
    {
        out << separator << "\n    " << Quote(key) << ": " << Quote(value);
        separator = ",";
    }
    out << "\n  },\n  \"warnings\": [";
    separator = "";
    for (const auto& warning : report.warnings)
    {
        out << separator << Quote(warning);
        separator = ", ";
    }
    out << "]\n}\n";
    return out.str();
}
} // namespace renderlab::rhi
