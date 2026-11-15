#include "DX11Simulation/Shader.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3dcompiler.h>

namespace
{
    std::filesystem::path executableDirectory()
    {
        // Long enough for paths beyond MAX_PATH.
        std::vector<wchar_t> path(32768);
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if(length == 0 || length >= path.size())
        {
            throw std::runtime_error(
                "GetModuleFileNameW failed while resolving a shader path (Win32 error "
                + std::to_string(GetLastError()) + ')'
            );
        }

        return std::filesystem::path(path.data()).parent_path();
    }

    std::string toString(const std::filesystem::path& path)
    {
        const auto utf8Str = path.u8string();

        return std::string(utf8Str.begin(), utf8Str.end());
    }

    class ShaderIncludes final : public ID3DInclude {
    private:
        struct OpenFile {
            std::filesystem::path directory;
            std::vector<char> bytes;
        };

        using OpenFiles = std::unordered_map<const void*, OpenFile>;

    private:
        std::filesystem::path sourceDirectory;
        std::filesystem::path sharedDirectory;
        OpenFiles openFiles;

    public:
        ShaderIncludes(std::filesystem::path sourceDirectory, std::filesystem::path sharedDirectory)
            : sourceDirectory(std::move(sourceDirectory)), sharedDirectory(std::move(sharedDirectory)) {}

        HRESULT __stdcall Open(
            D3D_INCLUDE_TYPE type,
            LPCSTR fileName,
            LPCVOID parentData,
            LPCVOID* data,
            UINT* byteCount
        ) override;
        HRESULT __stdcall Close(LPCVOID data) override
        {
            return openFiles.erase(data) != 0 ?
                S_OK :
                E_FAIL;
        }
    };

    HRESULT ShaderIncludes::Open(
        D3D_INCLUDE_TYPE, LPCSTR fileName,
        LPCVOID parentData,
        LPCVOID* data,
        UINT* byteCount)
    {
        try
        {
            const auto parent = openFiles.find(parentData);
            const auto& directory = parent != openFiles.end() ?
                parent->second.directory :
                sourceDirectory;
            const std::string_view text(fileName);
            const std::filesystem::path name(std::u8string(text.begin(), text.end()));

            auto path = directory / name;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if(!file)
            {
                path = sharedDirectory / name;
                file.open(path, std::ios::binary | std::ios::ate);
            }
            if(!file) return E_FAIL;

            const auto length = file.tellg();
            const bool readable = length >= 0
                && static_cast<std::uint64_t>(length) <= std::numeric_limits<UINT>::max();
            if(!readable) return E_FAIL;

            const auto size = static_cast<UINT>(length);
            // An empty include still needs a pointer of its own until Close.
            std::vector<char> bytes(size == 0 ? 1 : size);
            file.seekg(0);
            if(size != 0 && !file.read(bytes.data(), size)) return E_FAIL;

            const void* contents = bytes.data();
            openFiles.emplace(contents, OpenFile { path.parent_path(), std::move(bytes) });
            *data = contents;
            *byteCount = size;

            return S_OK;
        }
        catch(...)
        {
            return E_FAIL;
        }
    }

    using BlobRef = Microsoft::WRL::ComPtr<ID3DBlob>;

    BlobRef CompileShader(
        const Canape::ShaderDesc& desc,
        const char* target)
    {
        const auto directory = executableDirectory();
        const auto path = directory / desc.path;

        ShaderIncludes includes(path.parent_path(), directory / L"Shaders");
        BlobRef bytecode;
        BlobRef diagnostics;
        ThrowIfFailed(
            D3DCompileFromFile(
                path.c_str(),
                nullptr,
                &includes,
                desc.entryPoint.c_str(),
                target,
                D3DCOMPILE_PACK_MATRIX_ROW_MAJOR,
                0,
                &bytecode,
                &diagnostics
            ),
            std::format(
                "D3DCompileFromFile({}, entry={}, target={})",
                toString(desc.path),
                desc.entryPoint,
                target
            ),
            diagnostics != nullptr ?
                static_cast<const char*>(diagnostics->GetBufferPointer()) :
                nullptr
        );

        return bytecode;
    }
}

namespace Canape
{
    namespace
    {
        ThreadGroupSize reflectThreadGroupSize(ID3DBlob& bytecode)
        {
            RefPtr<ID3D11ShaderReflection> reflection;
            ThrowIfFailed(
                D3DReflect(
                    bytecode.GetBufferPointer(),
                    bytecode.GetBufferSize(),
                    IID_PPV_ARGS(&reflection)
                ),
                "D3DReflect"
            );
            UINT x = 0;
            UINT y = 0;
            UINT z = 0;
            reflection->GetThreadGroupSize(&x, &y, &z);

            return { x, y, z };
        }
    }

    VertexShaderRef CompileVertexShader(
        ID3D11Device& device,
        const ShaderDesc& desc,
        std::string_view name)
    {
        const auto bytecode = CompileShader(desc, "vs_5_0");

        VertexShaderRef shader;
        ThrowIfFailed(
            device.CreateVertexShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                &shader
            ),
            "ID3D11Device::CreateVertexShader"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            shader->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        return shader;
    }

    PixelShaderRef CompilePixelShader(
        ID3D11Device& device,
        const ShaderDesc& desc,
        std::string_view name)
    {
        const auto bytecode = CompileShader(desc, "ps_5_0");

        PixelShaderRef shader;
        ThrowIfFailed(
            device.CreatePixelShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                &shader
            ),
            "ID3D11Device::CreatePixelShader"
        );

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            shader->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif

        return shader;
    }

    ComputeShader::ComputeShader(
        ID3D11Device& device,
        const ShaderDesc& desc,
        std::string_view name)
    {
        const auto bytecode = CompileShader(desc, "cs_5_0");
        ThrowIfFailed(
            device.CreateComputeShader(
                bytecode->GetBufferPointer(),
                bytecode->GetBufferSize(),
                nullptr,
                &shader
            ),
            "ID3D11Device::CreateComputeShader"
        );
        threadGroupSize = reflectThreadGroupSize(*bytecode.Get());

    #if CANAPE_RENDER_DEBUG
        if(!name.empty())
        {
            shader->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>(name.length()),
                name.data()
            );
        }
    #endif
    }
}
