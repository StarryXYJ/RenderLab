#include "rhi/dx12/dx12_objects.hpp"
#include "rhi/dx12/dx12_runtime.hpp"

namespace renderlab::rhi::dx12
{
namespace
{
class DxSwapchain final : public Swapchain
{
public:
    DxSwapchain(DxRuntime& runtime, ID3D12Device* device, ID3D12CommandQueue* queue,
                IDXGIFactory6* factory, platform::NativeSurface surface, uint32_t width,
                uint32_t height, bool vsync)
        : Swapchain(&runtime), runtime_(runtime), queue_(queue), vsync_(vsync)
    {
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&presentFence_)),
              "Presentation fence");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 3;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (!vsync_)
        {
            ComPtr<IDXGIFactory5> factory5;
            BOOL supported = FALSE;
            if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))) &&
                SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &supported, sizeof(supported))) &&
                supported)
                flags_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        desc.Flags = flags_;
        ComPtr<IDXGISwapChain1> swapchain;
        Check(factory->CreateSwapChainForHwnd(queue, static_cast<HWND>(surface.window), &desc,
                                              nullptr, nullptr, &swapchain),
              "CreateSwapChainForHwnd");
        Check(swapchain.As(&native_), "Swapchain3");
        Check(factory->MakeWindowAssociation(static_cast<HWND>(surface.window),
                                             DXGI_MWA_NO_ALT_ENTER),
              "MakeWindowAssociation");
        Rebuild(width, height);
    }
    ~DxSwapchain() override
    {
        try
        {
            WaitPresentation();
        }
        catch (...)
        { /* Device loss: preserve the original failure. */
        }
    }
    AcquiredImage Acquire() override
    {
        if (images_.empty())
            return {};
        const auto index = native_->GetCurrentBackBufferIndex();
        return {images_[index], index};
    }
    void Present() override
    {
        Check(native_->Present(vsync_ ? 1 : 0, flags_ ? DXGI_PRESENT_ALLOW_TEARING : 0), "Present");
    }
    void Resize(uint32_t width, uint32_t height) override
    {
        WaitPresentation();
        views_.clear();
        images_.clear();
        if (!width || !height)
            return;
        Check(native_->ResizeBuffers(3, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, flags_),
              "ResizeBuffers");
        Rebuild(width, height);
    }
    Format GetFormat() const override
    {
        return Format::Rgba8Srgb;
    }

private:
    void WaitPresentation()
    {
        // Present enqueues work after the graphics completion signal. Drain that queue tail only
        // for resize/shutdown before releasing swapchain images, never during a normal frame.
        Check(queue_->Signal(presentFence_.Get(), ++presentPoint_),
              "Signal presentation retirement");
        Check(presentFence_->SetEventOnCompletion(presentPoint_, nullptr),
              "Wait presentation retirement");
    }
    void Rebuild(uint32_t width, uint32_t height)
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            auto image =
                std::make_shared<DxTexture>(owner, TextureDesc{width, height, Format::Rgba8Srgb,
                                                               false, true, "Swapchain.Image"});
            Check(native_->GetBuffer(i, IID_PPV_ARGS(&image->native)), "Swapchain GetBuffer");
            Check(image->native->SetName(L"Swapchain.Image"), "SetName");
            views_.push_back(runtime_.CreateView(image, TextureViewUsage::Default));
            images_.push_back(std::move(image));
        }
    }
    DxRuntime& runtime_;
    ID3D12CommandQueue* queue_;
    ComPtr<ID3D12Fence> presentFence_;
    uint64_t presentPoint_ = 0;
    bool vsync_;
    UINT flags_ = 0;
    ComPtr<IDXGISwapChain3> native_;
    std::vector<std::shared_ptr<Texture>> images_;
    std::vector<std::shared_ptr<TextureView>> views_;
};
} // namespace

std::unique_ptr<Swapchain> DxRuntime::CreateSwapchain(platform::NativeSurface surface,
                                                      uint32_t width, uint32_t height, bool vsync)
{
    return std::make_unique<DxSwapchain>(*this, device_, queue_, factory_, surface, width, height,
                                         vsync);
}
} // namespace renderlab::rhi::dx12
