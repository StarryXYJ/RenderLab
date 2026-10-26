#include "shader/dxc_compiler.hpp"

#include "platform/text.hpp"

#include <Windows.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <stdexcept>
#include <utility>

namespace renderlab::shader
{
namespace
{
using Microsoft::WRL::ComPtr;
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::format("{} failed (0x{:08X})", operation, uint32_t(result)));
}
class Includes final : public IDxcIncludeHandler
{
public:
    Includes(IDxcUtils* utils, std::string generated, std::string types)
        : utils_(utils), generated_(std::move(generated)), types_(std::move(types))
    {
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override
    {
        if (id == __uuidof(IUnknown) || id == __uuidof(IDxcIncludeHandler))
        {
            *result = this;
            AddRef();
            return S_OK;
        }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references_;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR name, IDxcBlob** result) override
    {
        try
        {
            auto path = std::filesystem::path(name);
            auto logical = ToUtf8(path);
            std::ranges::replace(logical, '\\', '/');
            std::string text;
            if (logical.ends_with("renderlab/generated/parameters.hlsli"))
                text = generated_;
            else if (logical.ends_with("renderlab/generated/types.hlsli"))
                text = types_;
            else
            {
                path = std::filesystem::weakly_canonical(path);
                text = ReadText(path);
                if (text.size() > 4 * 1024 * 1024 || dependencies.size() >= 256)
                    return E_OUTOFMEMORY;
                dependencies.insert_or_assign(path, text);
            }
            ComPtr<IDxcBlobEncoding> blob;
            Check(utils_->CreateBlob(text.data(), static_cast<UINT32>(text.size()), DXC_CP_UTF8,
                                     &blob),
                  "Create include blob");
            *result = blob.Detach();
            return S_OK;
        }
        catch (...)
        {
            *result = nullptr;
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
    }
    std::map<std::filesystem::path, std::string> dependencies;

private:
    std::atomic<ULONG> references_{1};
    ComPtr<IDxcUtils> utils_;
    std::string generated_, types_;
};
std::vector<std::wstring> Arguments(const ProgramRequest& request, bool preprocess)
{
    const auto stage = request.entry.stage == rhi::ShaderStage::Vertex  ? L"vs_6_0"
                       : request.entry.stage == rhi::ShaderStage::Pixel ? L"ps_6_0"
                                                                        : L"cs_6_0";
    std::vector<std::wstring> args{request.entry.source.wstring(),
                                   L"-E",
                                   platform::Utf8ToWide(request.entry.function),
                                   L"-T",
                                   stage,
                                   L"-HV",
                                   L"2021",
                                   L"-Ges",
                                   L"-WX",
                                   L"-O3",
                                   L"-Zpc",
                                   L"-I",
                                   request.entry.source.parent_path().wstring()};
    if (request.target == Target::Spirv)
    {
        args.insert(args.end(), {L"-spirv", L"-fspv-target-env=vulkan1.3", L"-fvk-use-dx-layout"});
    }
    for (const auto& path : request.includeDirectories)
    {
        args.push_back(L"-I");
        args.push_back(path.wstring());
    }
    for (const auto& [name, value] : request.defines)
    {
        args.push_back(L"-D");
        args.push_back(platform::Utf8ToWide(name + "=" + value));
    }
    if (preprocess)
        args.push_back(L"-P");
    return args;
}
} // namespace
struct DxcCompiler::Impl
{
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcResult> Run(const ProgramRequest& request, bool preprocess, Includes& includes)
    {
        const auto text = request.sourceSnapshot.empty() ? ReadText(request.entry.source)
                                                         : request.sourceSnapshot;
        DxcBuffer buffer{text.data(), text.size(), DXC_CP_UTF8};
        auto storage = Arguments(request, preprocess);
        std::vector<LPCWSTR> args;
        for (const auto& a : storage)
            args.push_back(a.c_str());
        ComPtr<IDxcResult> result;
        Check(compiler->Compile(&buffer, args.data(), static_cast<UINT32>(args.size()), &includes,
                                IID_PPV_ARGS(&result)),
              "DXC Compile");
        HRESULT status{};
        Check(result->GetStatus(&status), "DXC status");
        if (FAILED(status))
        {
            ComPtr<IDxcBlobUtf8> errors;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            throw std::runtime_error(errors && errors->GetStringLength()
                                         ? errors->GetStringPointer()
                                         : "Shader compilation failed");
        }
        return result;
    }
};
DxcCompiler::DxcCompiler() : impl_(std::make_unique<Impl>())
{
    Check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&impl_->utils)), "Create DXC utils");
    Check(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&impl_->compiler)),
          "Create DXC compiler");
}
DxcCompiler::~DxcCompiler() = default;
std::string DxcCompiler::Version() const
{
    ComPtr<IDxcVersionInfo> version;
    Check(impl_->compiler.As(&version), "DXC version interface");
    UINT32 major{}, minor{}, flags{};
    version->GetVersion(&major, &minor);
    version->GetFlags(&flags);
    ComPtr<IDxcVersionInfo2> details;
    std::string commit;
    UINT32 commitCount{};
    if (SUCCEEDED(version.As(&details)))
    {
        char* hash{};
        if (SUCCEEDED(details->GetCommitInfo(&commitCount, &hash)))
        {
            commit = hash;
            CoTaskMemFree(hash);
        }
    }
    return std::format("{}.{}:{}:{}:{}", major, minor, flags, commitCount, commit);
}
Preprocessed DxcCompiler::Preprocess(const ProgramRequest& request)
{
    ComPtr<Includes> includes;
    includes.Attach(new Includes(impl_->utils.Get(), request.generated, request.generatedTypes));
    auto result = impl_->Run(request, true, *includes.Get());
    ComPtr<IDxcBlobUtf8> text;
    Check(result->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&text), nullptr), "Preprocess output");
    Preprocessed output;
    output.text.assign(text->GetStringPointer(), text->GetStringLength());
    output.dependencies.push_back({request.entry.source, request.sourceSnapshot.empty()
                                                             ? ReadText(request.entry.source)
                                                             : request.sourceSnapshot});
    for (const auto& [path, content] : includes->dependencies)
        output.dependencies.push_back({path, content});
    return output;
}
std::vector<std::byte> DxcCompiler::Compile(const ProgramRequest& request)
{
    ComPtr<Includes> includes;
    includes.Attach(new Includes(impl_->utils.Get(), request.generated, request.generatedTypes));
    auto result = impl_->Run(request, false, *includes.Get());
    ComPtr<IDxcBlob> object;
    Check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr), "DXC object");
    std::vector<std::byte> code(object->GetBufferSize());
    std::memcpy(code.data(), object->GetBufferPointer(), code.size());
    return code;
}
} // namespace renderlab::shader
