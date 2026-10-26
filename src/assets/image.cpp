#include "assets/model.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <stdexcept>

namespace renderlab::assets
{
namespace
{
void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("WIC image decode failed");
}
} // namespace
Image DecodeImage(const std::vector<std::byte>& bytes)
{
    if (bytes.empty() || bytes.size() > std::numeric_limits<DWORD>::max())
        throw std::invalid_argument("Invalid image bytes");
    APTTYPE apartment;
    APTTYPEQUALIFIER qualifier;
    const auto existing = CoGetApartmentType(&apartment, &qualifier);
    // Reuse a host-owned STA/MTA. Only standalone import threads own a temporary apartment.
    const auto initialized =
        existing == CO_E_NOTINITIALIZED ? CoInitializeEx(nullptr, COINIT_MULTITHREADED) : existing;
    struct Cleanup
    {
        bool release;
        ~Cleanup()
        {
            if (release)
                CoUninitialize();
        }
    } cleanup{existing == CO_E_NOTINITIALIZED && SUCCEEDED(initialized)};
    Check(initialized);
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream));
    Check(
        stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<std::byte*>(bytes.data())),
                                     static_cast<DWORD>(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                           &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame));
    Image result;
    Check(frame->GetSize(&result.width, &result.height));
    if (!result.width || !result.height || result.width > 8192 || result.height > 8192)
        throw std::invalid_argument("Image extent outside 1..8192");
    result.rgba.resize(size_t(result.width) * result.height * 4);
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter));
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom));
    Check(converter->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.rgba.size()),
                                result.rgba.data()));
    return result;
}
} // namespace renderlab::assets
