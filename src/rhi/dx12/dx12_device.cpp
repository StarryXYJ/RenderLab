#include "rhi/device.hpp"
#include "rhi/dx12/dx12_runtime.hpp"

#include "platform/text.hpp"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstddef>
#include <format>
#include <stdexcept>

namespace renderlab::rhi
{
namespace
{
using dx12::Check;
using Microsoft::WRL::ComPtr;

class Dx12Device final : public Device
{
public:
    explicit Dx12Device(Logger& logger) : logger_(logger) {}

    ~Dx12Device() override
    {
        Shutdown();
    }

    void Shutdown() noexcept override
    {
        queue_.Reset();
        try
        {
            PollDiagnostics();
        }
        catch (...)
        {
            ++validationErrors_;
        }
        infoQueue_.Reset();
        device_.Reset();
        adapter_.Reset();
        factory_.Reset();
    }

    void Initialize(const DeviceOptions& options)
    {
        report_.backend = "dx12";
        report_.apiVersion = "Direct3D 12 / feature level 12_0 minimum";
        // Configure validation before device creation; only auto mode permits an explicit fallback.
        if (options.validation != ValidationMode::Off)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                report_.validationEnabled = true;
            }
            else if (options.validation == ValidationMode::On)
            {
                throw std::runtime_error(
                    "D3D12 debug layer unavailable; install Windows Graphics Tools");
            }
            else
            {
                Warn("D3D12 debug layer unavailable; validation auto continues without it");
            }
        }
        UINT factoryFlags = report_.validationEnabled ? DXGI_CREATE_FACTORY_DEBUG : 0;
        auto result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
        if (FAILED(result) && factoryFlags != 0 && options.validation == ValidationMode::Auto)
        {
            Warn("DXGI factory debug unavailable; D3D12 validation remains enabled");
            factoryFlags = 0;
            result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
        }
        Check(result, "CreateDXGIFactory2");
        report_.details.emplace_back("dxgiFactoryDebug",
                                     factoryFlags != 0 ? "enabled" : "disabled");
        report_.details.emplace_back("adapterOrder", "DXGI high-performance preference");

        std::vector<ComPtr<IDXGIAdapter1>> adapters;
        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            result = factory_->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            Check(result, "EnumAdapterByGpuPreference");
            DXGI_ADAPTER_DESC1 desc{};
            Check(adapter->GetDesc1(&desc), "GetDesc1");
            report_.adapters.push_back({index, platform::WideToUtf8(desc.Description),
                                        desc.VendorId, desc.DeviceId,
                                        static_cast<uint64_t>(desc.DedicatedVideoMemory),
                                        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0});
            adapters.push_back(std::move(adapter));
        }
        if (options.adapterIndex && *options.adapterIndex >= adapters.size())
        {
            throw std::runtime_error(
                std::format("DX12 adapter index {} is out of range ({} adapters)",
                            *options.adapterIndex, adapters.size()));
        }
        for (size_t index = 0; index < adapters.size(); ++index)
        {
            if (options.adapterIndex ? index != *options.adapterIndex
                                     : report_.adapters[index].software)
            {
                continue;
            }
            result = D3D12CreateDevice(adapters[index].Get(), D3D_FEATURE_LEVEL_12_0,
                                       IID_PPV_ARGS(&device_));
            if (SUCCEEDED(result))
            {
                adapter_ = adapters[index];
                report_.selectedAdapter = static_cast<uint32_t>(index);
                break;
            }
            if (options.adapterIndex)
            {
                Check(result, "D3D12CreateDevice on selected adapter");
            }
        }
        if (!device_)
        {
            throw std::runtime_error("No hardware adapter supports Direct3D feature level 12_0");
        }
        if (report_.validationEnabled)
        {
            Check(device_.As(&infoQueue_), "Query ID3D12InfoQueue");
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
        Check(device_->SetName(L"RenderLab.Device"), "Device SetName");
        Check(queue_->SetName(L"RenderLab.GraphicsQueue"), "Queue SetName");
        report_.queues.push_back({"created direct queue (not a hardware queue-family count)", 0, 1,
                                  true, true, true, true});

        LARGE_INTEGER driver{};
        if (SUCCEEDED(adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
        {
            report_.driverVersion =
                std::format("{}.{}.{}.{}", HIWORD(driver.HighPart), LOWORD(driver.HighPart),
                            HIWORD(driver.LowPart), LOWORD(driver.LowPart));
        }
        else
        {
            report_.driverVersion = "unavailable from DXGI";
        }

        D3D12_FEATURE_DATA_SHADER_MODEL shader{};
        // Older query enums may be required; report confirmed support, not capabilities beyond the
        // SDK.
        for (const auto model : {D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6,
                                 D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3,
                                 D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0})
        {
            shader.HighestShaderModel = model;
            if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader,
                                                       sizeof(shader))))
            {
                break;
            }
            shader.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0);
        }
        if (shader.HighestShaderModel < D3D_SHADER_MODEL_6_0)
        {
            throw std::runtime_error("Shader Model 6.0 is required");
        }
        const auto shaderModel = static_cast<uint32_t>(shader.HighestShaderModel);
        report_.shaderTarget = std::format("Shader Model {}.{} supported; M1 DXC target 6.0",
                                           shaderModel >> 4, shaderModel & 15);

        D3D12_FEATURE_DATA_D3D12_OPTIONS base{};
        Check(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &base, sizeof(base)),
              "Options");
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 rayTracing{};
        device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &rayTracing, sizeof(rayTracing));
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 mesh{};
        device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &mesh, sizeof(mesh));
        auto& caps = report_.supported;
        caps.indirectDraw = true;
        caps.multiDrawIndirect = true;
        caps.indirectCount = true;
        caps.indirectFirstInstance = true;
        caps.descriptorIndexing = base.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
        caps.accelerationStructure =
            rayTracing.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        caps.rayTracingPipeline = caps.accelerationStructure;
        caps.rayQuery = rayTracing.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
        caps.meshShader = mesh.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        caps.maxTexture2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        caps.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        report_.details.emplace_back("resourceBindingTier",
                                     std::to_string(base.ResourceBindingTier));
        report_.details.emplace_back("rayTracingTier", std::to_string(rayTracing.RaytracingTier));
        report_.details.emplace_back("enabledWork", "one direct queue; M1 graphics, compute, copy "
                                                    "and indirect draw; no ray tracing commands");
        report_.details.emplace_back("descriptorIndexingScope",
                                     "resource arrays; shader and tier limits still apply");

        for (const auto& [name, format] :
             {std::pair{"RGBA8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM},
              std::pair{"RGBA16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT},
              std::pair{"RGBA32_FLOAT", DXGI_FORMAT_R32G32B32A32_FLOAT},
              std::pair{"D32_FLOAT", DXGI_FORMAT_D32_FLOAT},
              std::pair{"D32_FLOAT_S8X24_UINT", DXGI_FORMAT_D32_FLOAT_S8X24_UINT}})
        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = format;
            Check(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support,
                                               sizeof(support)),
                  "FormatSupport");
            auto sampled = support;
            if (format == DXGI_FORMAT_D32_FLOAT)
                sampled.Format = DXGI_FORMAT_R32_FLOAT;
            else if (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                sampled.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            if (sampled.Format != format)
                Check(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &sampled,
                                                   sizeof(sampled)),
                      "Depth sampled format support");
            report_.formats.push_back(
                {name, (sampled.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0,
                 (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0,
                 (support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0,
                 (support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0});
        }
    }

    const DeviceReport& GetReport() const override
    {
        return report_;
    }
    std::unique_ptr<Runtime> CreateRuntime() override
    {
        return CreateDx12Runtime(device_.Get(), queue_.Get(), factory_.Get());
    }
    uint32_t GetValidationErrorCount() const override
    {
        return validationErrors_;
    }

    void PollDiagnostics() override
    {
        if (!infoQueue_)
        {
            return;
        }
        const auto count = infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T size = 0;
            Check(infoQueue_->GetMessage(index, nullptr, &size), "GetMessage size");
            // Messages have variable size; their backing storage must also satisfy struct
            // alignment.
            std::vector<std::max_align_t> storage((size + sizeof(std::max_align_t) - 1) /
                                                  sizeof(std::max_align_t));
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(infoQueue_->GetMessage(index, message, &size), "GetMessage");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            {
                ++validationErrors_;
                logger_.Write(LogLevel::Error, message->pDescription);
            }
            else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
            {
                logger_.Write(LogLevel::Warning, message->pDescription);
            }
        }
        infoQueue_->ClearStoredMessages();
    }

private:
    void Warn(std::string message)
    {
        logger_.Write(LogLevel::Warning, message);
        report_.warnings.push_back(std::move(message));
    }

    Logger& logger_;
    DeviceReport report_;
    uint32_t validationErrors_ = 0;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    ComPtr<ID3D12CommandQueue> queue_;
};
} // namespace

std::unique_ptr<Device> CreateDx12Device(const DeviceOptions& options, Logger& logger)
{
    auto device = std::make_unique<Dx12Device>(logger);
    device->Initialize(options);
    return device;
}
} // namespace renderlab::rhi
