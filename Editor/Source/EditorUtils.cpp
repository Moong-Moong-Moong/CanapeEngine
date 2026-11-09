#include "EditorUtils.h"

#include <DirectXMath.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <TlHelp32.h>

#include <cmath>

using namespace Canape;

namespace
{
    // Runs a command line without a console window. output (optional) receives stdout and stderr.
    bool RunProcess(std::wstring commandLine, DWORD waitMilliseconds, DWORD& exitCode, std::string* output)
    {
        SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (output)
        {
            if (!CreatePipe(&readPipe, &writePipe, &security, 0))
            {
                return false;
            }
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if (output)
        {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = writePipe;
            startup.hStdError = writePipe;
        }

        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, output != nullptr,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        if (writePipe)
        {
            CloseHandle(writePipe);
        }
        if (!started)
        {
            if (readPipe)
            {
                CloseHandle(readPipe);
            }
            return false;
        }

        if (readPipe)
        {
            char buffer[512];
            DWORD read = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
            {
                output->append(buffer, read);
            }
            CloseHandle(readPipe);
        }

        exitCode = STILL_ACTIVE;
        if (waitMilliseconds > 0)
        {
            WaitForSingleObject(process.hProcess, waitMilliseconds);
            GetExitCodeProcess(process.hProcess, &exitCode);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    std::wstring Quote(const std::wstring& text)
    {
        return L"\"" + text + L"\"";
    }

    const std::wstring& FindVisualStudio()
    {
        static const std::wstring devenv = []
        {
            wchar_t programFiles[MAX_PATH] = {};
            if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, MAX_PATH))
            {
                return std::wstring();
            }
            const std::wstring vswhere = std::wstring(programFiles) + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
            DWORD exitCode = 0;
            std::string output;
            if (!RunProcess(Quote(vswhere) + L" -latest -property productPath", 10000, exitCode, &output) || exitCode != 0)
            {
                return std::wstring();
            }
            while (!output.empty() && (output.back() == '\r' || output.back() == '\n'))
            {
                output.pop_back();
            }
            return EditorUtils::FromUtf8(output).wstring();
        }();
        return devenv;
    }

    bool IsProcessRunning(const wchar_t* exeName)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry))
        {
            found = _wcsicmp(entry.szExeFile, exeName) == 0;
        }
        CloseHandle(snapshot);
        return found;
    }
}

namespace EditorUtils
{
    Vector3 QuaternionToEuler(const Quaternion& rotation)
    {
        const Quaternion q = rotation.Normalized();
        const float sinPitch = 2.0f * (q.W * q.X - q.Y * q.Z);
        const float pitch = std::abs(sinPitch) >= 1.0f ? std::copysign(Math::Pi * 0.5f, sinPitch) : std::asin(sinPitch);
        const float yaw = std::atan2(2.0f * (q.W * q.Y + q.X * q.Z), 1.0f - 2.0f * (q.X * q.X + q.Y * q.Y));
        const float roll = std::atan2(2.0f * (q.W * q.Z + q.X * q.Y), 1.0f - 2.0f * (q.X * q.X + q.Z * q.Z));
        return { Math::ToDegrees(pitch), Math::ToDegrees(yaw), Math::ToDegrees(roll) };
    }

    Quaternion EulerToQuaternion(const Vector3& degrees)
    {
        return Rotator{ degrees.X, degrees.Y, degrees.Z }.ToQuaternion();
    }

    bool CanReparent(const TransformComponent* child, const TransformComponent* parent)
    {
        if (!child || child == parent)
        {
            return false;
        }
        return !parent || !parent->IsChildOf(child);
    }

    bool SetParentKeepWorld(TransformComponent* child, TransformComponent* parent)
    {
        if (!CanReparent(child, parent) || child->GetParent() == parent)
        {
            return false;
        }

        const Matrix4 world = child->GetLocalToWorldMatrix();
        const Matrix4 local = parent ? world * parent->GetWorldToLocalMatrix() : world;

        DirectX::XMVECTOR scale;
        DirectX::XMVECTOR rotation;
        DirectX::XMVECTOR translation;
        if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, local.ToXM()))
        {
            child->SetLocalPosition(Vector3::FromXM(translation));
            child->SetLocalRotation(Quaternion::FromXM(rotation).Normalized());
            child->SetLocalScale(Vector3::FromXM(scale));
        }

        child->SetParent(parent);
        return true;
    }

    std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    std::filesystem::path FromUtf8(const std::string& text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    const std::filesystem::path& GetGameDirectory()
    {
        static const std::filesystem::path directory = FromUtf8(CANAPE_GAME_DIR).lexically_normal();
        return directory;
    }

    std::filesystem::path GetSceneDirectory()
    {
        return GetGameDirectory() / "Content" / "Scenes";
    }

    std::string ToGameRelativePath(const std::filesystem::path& path)
    {
        const std::filesystem::path relative = path.lexically_normal().lexically_relative(GetGameDirectory());
        if (relative.empty() || *relative.begin() == "..")
        {
            return ToUtf8(path);
        }
        return ToUtf8(relative);
    }

    bool RunTool(const std::string& script, const std::wstring& arguments, std::string& error)
    {
        const std::filesystem::path path = (GetGameDirectory() / ".." / "Tools" / script).lexically_normal();
        const std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " + Quote(path.wstring()) + L" " + arguments;

        DWORD exitCode = 1;
        std::string output;
        if (!RunProcess(command, 30000, exitCode, &output))
        {
            error = "Could not start " + script;
            return false;
        }
        if (exitCode != 0)
        {
            // PowerShell prints the thrown message first.
            const size_t end = output.find_first_of("\r\n");
            error = script + " failed: " + (end == std::string::npos ? output : output.substr(0, end));
            return false;
        }
        return true;
    }

    void OpenInVisualStudio(const std::filesystem::path& file)
    {
        const std::wstring& devenv = FindVisualStudio();
        if (!devenv.empty())
        {
            const std::filesystem::path solution = (GetGameDirectory() / ".." / "Canape.slnx").lexically_normal();
            const std::wstring command = IsProcessRunning(L"devenv.exe")
                ? Quote(devenv) + L" /Edit " + Quote(file.wstring())
                : Quote(devenv) + L" " + Quote(solution.wstring()) + L" /Command " + Quote(L"File.OpenFile " + file.wstring());
            DWORD exitCode = 0;
            if (RunProcess(command, 0, exitCode, nullptr))
            {
                return;
            }
        }
        ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
