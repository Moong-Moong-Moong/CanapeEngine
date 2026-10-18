#pragma once

#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>

namespace Canape
{
    // relativePath is relative to the executable, never the process working directory.
    // Bytecode is temporary; GPU shaders and input layouts are owned by DX11PipelineState.
    bool CompileShaderFile(const wchar_t* relativePath, const char* entryPoint, const char* target,
        const D3D_SHADER_MACRO* macros, UINT flags,
        Microsoft::WRL::ComPtr<ID3DBlob>& bytecode, std::string& error);
}
