#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DX11Renderer/Pipeline/DX11ShaderCompiler.h"
#include "CNDebug/DebugLog.h"

#include <filesystem>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace Canape
{
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

        Microsoft::WRL::ComPtr<ID3DBlob> diagnostics;
        const HRESULT result = D3DCompileFromFile(path.c_str(), macros,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, flags, 0,
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
