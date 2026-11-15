#include "DX11Simulation/D3D.h"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    std::string fromUTF16String(std::wstring_view utf16Str)
    {
        const auto length = WideCharToMultiByte(
            CP_UTF8,
            0,
            utf16Str.data(),
            static_cast<int>(utf16Str.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );
        std::string utf8Str(length, '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            utf16Str.data(),
            static_cast<int>(utf16Str.size()),
            utf8Str.data(),
            length,
            nullptr,
            nullptr
        );

        return utf8Str;
    }

    std::string HResultToString(HRESULT result)
    {
        wchar_t* message = nullptr;
        const auto length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM
                | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(result),
            0,
            reinterpret_cast<wchar_t*>(&message),
            0,
            nullptr
        );
        if(length == 0)
            return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));

        auto text = fromUTF16String(std::wstring_view(message, length));
        LocalFree(message);
        while(!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();

        return text;
    }
}

namespace Canape::detail
{
    void ThrowHResult(HRESULT result, std::string_view msg)
    {
        throw std::runtime_error(
            std::format("{}: {}",
                msg,
                HResultToString(result)
            )
        );
    }

    void ThrowHResult(HRESULT result, std::string_view msg, std::string_view detail)
    {
        if(!detail.empty())
        {
            throw std::runtime_error(
                std::format("{}: {}\n{}",
                    msg,
                    HResultToString(result),
                    detail
                )
            );
        }
        else
        {
            ThrowHResult(result, msg);
        }
    }
}
