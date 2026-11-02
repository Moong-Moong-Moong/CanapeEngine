#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DX11Renderer/Pipeline/DX11ShaderCompiler.h"
#include "CNDebug/DebugLog.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>
#include <sstream>
#include <string_view>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace Canape
{
    namespace
    {
        // Resolve nested includes relative to their parent file, including Unicode paths.
        class ShaderFileIncludes final : public ID3DInclude
        {
        public:
            explicit ShaderFileIncludes(const std::filesystem::path& sourcePath)
                : m_SourceDirectory(sourcePath.parent_path())
            {
            }

            HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID parentData,
                LPCVOID* data, UINT* byteCount) override
            {
                try
                {
                    const auto parent = m_OpenFiles.find(parentData);
                    const auto& directory = parent != m_OpenFiles.end()
                        ? parent->second.Directory : m_SourceDirectory;
                    const std::string_view includeName(fileName);
                    const auto path = directory / std::filesystem::path(
                        std::u8string(includeName.begin(), includeName.end()));
                    std::ifstream file(path, std::ios::binary | std::ios::ate);
                    if (!file) return E_FAIL;
                    const auto length = file.tellg();
                    if (length < 0 || static_cast<uint64_t>(length) > std::numeric_limits<UINT>::max())
                        return E_FAIL;
                    const UINT size = static_cast<UINT>(length);
                    // Even an empty include needs a unique pointer until Close.
                    std::vector<char> bytes(size == 0 ? 1 : size);
                    file.seekg(0);
                    if (size != 0 && !file.read(bytes.data(), size)) return E_FAIL;
                    const void* contents = bytes.data();
                    m_OpenFiles.emplace(contents, IncludeFile{ path.parent_path(), std::move(bytes) });
                    *data = contents;
                    *byteCount = size;
                    return S_OK;
                }
                catch (...)
                {
                    return E_FAIL;
                }
            }

            HRESULT __stdcall Close(LPCVOID data) override
            {
                return m_OpenFiles.erase(data) != 0 ? S_OK : E_FAIL;
            }

        private:
            struct IncludeFile
            {
                std::filesystem::path Directory;
                std::vector<char> Bytes;
            };

            std::filesystem::path m_SourceDirectory;
            std::unordered_map<const void*, IncludeFile> m_OpenFiles;
        };
    }

    bool CompileShaderFile(const wchar_t* relativePath, const char* entryPoint, const char* target,
        const D3D_SHADER_MACRO* macros, UINT flags,
        Microsoft::WRL::ComPtr<ID3DBlob>& bytecode, std::string& error)
    {
        bytecode.Reset();
        error.clear();

        // Unicode paths, including executable paths longer than MAX_PATH.
        std::vector<wchar_t> executablePath(32768);
        const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
            static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length >= executablePath.size())
        {
            error = "GetModuleFileNameW failed while resolving shader path (Win32 error "
                + std::to_string(GetLastError()) + ')';
            CN_LOG_RENDERER("Shader", error);
            return false;
        }
        const auto path = std::filesystem::path(executablePath.data()).parent_path() / relativePath;

        ShaderFileIncludes includes(path);
        Microsoft::WRL::ComPtr<ID3DBlob> diagnostics;
        const HRESULT result = D3DCompileFromFile(path.c_str(), macros,
            &includes, entryPoint, target, flags, 0,
            &bytecode, &diagnostics);
        if (FAILED(result))
        {
            const auto utf8Path = path.u8string();
            std::ostringstream message;
            message << "D3DCompileFromFile failed: "
                << std::string(utf8Path.begin(), utf8Path.end())
                << " [entry=" << entryPoint << ", target=" << target
                << ", HRESULT=0x" << std::hex << static_cast<unsigned long>(result) << ']';
            if (diagnostics)
                message << '\n' << static_cast<const char*>(diagnostics->GetBufferPointer());
            error = message.str();
            CN_LOG_RENDERER("Shader", error);
            bytecode.Reset();
            return false;
        }
        return true;
    }
}
