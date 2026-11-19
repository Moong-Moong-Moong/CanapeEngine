#include "EditorUtils.h"
#include "CNDebug/DebugLog.h"

#include <DirectXMath.h>

#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <wrl/client.h>

#include <cmath>
#include <cwctype>
#include <thread>

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

    HRESULT InvokeDispatch(IDispatch* target, const wchar_t* name, WORD flags, VARIANT* result, VARIANT* args = nullptr, UINT argCount = 0)
    {
        DISPID id = 0;
        LPOLESTR member = const_cast<LPOLESTR>(name);
        HRESULT hr = target->GetIDsOfNames(IID_NULL, &member, 1, LOCALE_USER_DEFAULT, &id);
        if (FAILED(hr))
        {
            return hr;
        }
        DISPPARAMS params{ args, nullptr, argCount, 0 };
        return target->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, nullptr, nullptr);
    }

    Microsoft::WRL::ComPtr<IDispatch> GetDispatchProperty(IDispatch* target, const wchar_t* name)
    {
        VARIANT result;
        VariantInit(&result);
        Microsoft::WRL::ComPtr<IDispatch> property;
        if (SUCCEEDED(InvokeDispatch(target, name, DISPATCH_PROPERTYGET, &result)) && result.vt == VT_DISPATCH)
        {
            property = result.pdispVal;
        }
        VariantClear(&result);
        return property;
    }

    std::wstring GetStringProperty(IDispatch* target, const wchar_t* name)
    {
        VARIANT result;
        VariantInit(&result);
        std::wstring text;
        if (SUCCEEDED(InvokeDispatch(target, name, DISPATCH_PROPERTYGET, &result)) && result.vt == VT_BSTR && result.bstrVal)
        {
            text.assign(result.bstrVal, SysStringLen(result.bstrVal));
        }
        VariantClear(&result);
        return text;
    }

    std::wstring NormalizePathKey(const std::filesystem::path& path)
    {
        std::wstring key = path.lexically_normal().wstring();
        for (wchar_t& c : key)
        {
            c = c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c));
        }
        return key;
    }

    Microsoft::WRL::ComPtr<IDispatch> FindVisualStudioWithSolution(const std::filesystem::path& solution)
    {
        Microsoft::WRL::ComPtr<IRunningObjectTable> table;
        Microsoft::WRL::ComPtr<IEnumMoniker> monikers;
        Microsoft::WRL::ComPtr<IBindCtx> bindContext;
        if (FAILED(GetRunningObjectTable(0, &table)) || FAILED(table->EnumRunning(&monikers))
            || FAILED(CreateBindCtx(0, &bindContext)))
        {
            return nullptr;
        }

        const std::wstring solutionKey = NormalizePathKey(solution);
        Microsoft::WRL::ComPtr<IMoniker> moniker;
        while (monikers->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK)
        {
            LPOLESTR displayName = nullptr;
            if (FAILED(moniker->GetDisplayName(bindContext.Get(), nullptr, &displayName)))
            {
                continue;
            }
            const bool isVisualStudio = std::wstring_view(displayName).starts_with(L"!VisualStudio.DTE.");
            CoTaskMemFree(displayName);
            if (!isVisualStudio)
            {
                continue;
            }

            Microsoft::WRL::ComPtr<IUnknown> object;
            Microsoft::WRL::ComPtr<IDispatch> dte;
            if (FAILED(table->GetObject(moniker.Get(), &object)) || FAILED(object.As(&dte)))
            {
                continue;
            }

            const Microsoft::WRL::ComPtr<IDispatch> openSolution = GetDispatchProperty(dte.Get(), L"Solution");
            if (openSolution && NormalizePathKey(GetStringProperty(openSolution.Get(), L"FullName")) == solutionKey)
            {
                return dte;
            }
        }
        return nullptr;
    }

    HRESULT InvokeWithString(IDispatch* target, const wchar_t* method, const std::wstring& text, VARIANT* result)
    {
        VARIANT argument;
        VariantInit(&argument);
        argument.vt = VT_BSTR;
        argument.bstrVal = SysAllocString(text.c_str());
        const HRESULT hr = InvokeDispatch(target, method, DISPATCH_METHOD, result, &argument, 1);
        VariantClear(&argument);
        return hr;
    }

    bool IsFileInLoadedProject(IDispatch* dte, const std::filesystem::path& file)
    {
        const Microsoft::WRL::ComPtr<IDispatch> solution = GetDispatchProperty(dte, L"Solution");
        if (!solution)
        {
            return false;
        }
        VARIANT result;
        VariantInit(&result);
        const bool found = SUCCEEDED(InvokeWithString(solution.Get(), L"FindProjectItem", file.wstring(), &result))
            && result.vt == VT_DISPATCH && result.pdispVal;
        VariantClear(&result);
        return found;
    }

    void ActivateMainWindow(IDispatch* dte)
    {
        const Microsoft::WRL::ComPtr<IDispatch> mainWindow = GetDispatchProperty(dte, L"MainWindow");
        if (!mainWindow)
        {
            return;
        }
        VARIANT handle;
        VariantInit(&handle);
        if (SUCCEEDED(InvokeDispatch(mainWindow.Get(), L"HWnd", DISPATCH_PROPERTYGET, &handle))
            && SUCCEEDED(VariantChangeType(&handle, &handle, 0, VT_I8)))
        {
            HWND hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(handle.llVal));
            if (IsIconic(hwnd))
            {
                ShowWindow(hwnd, SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
        }
        VariantClear(&handle);
    }

    bool OpenFileWithDte(IDispatch* dte, const std::filesystem::path& file)
    {
        const Microsoft::WRL::ComPtr<IDispatch> itemOperations = GetDispatchProperty(dte, L"ItemOperations");
        if (!itemOperations)
        {
            return false;
        }

        VARIANT result;
        VariantInit(&result);
        const HRESULT hr = InvokeWithString(itemOperations.Get(), L"OpenFile", file.wstring(), &result);
        VariantClear(&result);
        if (FAILED(hr))
        {
            return false;
        }

        VariantInit(&result);
        InvokeWithString(dte, L"ExecuteCommand", L"SolutionExplorer.SyncWithActiveDocument", &result);
        VariantClear(&result);

        ActivateMainWindow(dte);
        return true;
    }

    HWND FindSolutionWindow(const std::wstring& solutionName)
    {
        struct Search
        {
            std::wstring prefix;
            HWND found = nullptr;
        } search{ solutionName + L" - ", nullptr };

        EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL
        {
            auto* state = reinterpret_cast<Search*>(parameter);
            if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
            {
                return TRUE;
            }
            wchar_t title[512] = {};
            GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
            const std::wstring_view text(title);
            if (text.starts_with(state->prefix) && text.find(L"Microsoft Visual Studio") != std::wstring_view::npos)
            {
                state->found = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    void OpenWhenSolutionLoads(const std::filesystem::path& solution, const std::filesystem::path& file)
    {
        std::thread([solution, file]
        {
            const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            for (int attempt = 0; attempt < 180; ++attempt)
            {
                Sleep(1000);
                const Microsoft::WRL::ComPtr<IDispatch> dte = FindVisualStudioWithSolution(solution);
                if (dte && IsFileInLoadedProject(dte.Get(), file) && OpenFileWithDte(dte.Get(), file))
                {
                    break;
                }
            }
            if (SUCCEEDED(init))
            {
                CoUninitialize();
            }
        }).detach();
    }

    bool OpenInSolutionInstance(const std::filesystem::path& solution, const std::filesystem::path& file)
    {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool opened = false;
        if (const Microsoft::WRL::ComPtr<IDispatch> dte = FindVisualStudioWithSolution(solution))
        {
            opened = OpenFileWithDte(dte.Get(), file);
        }
        if (SUCCEEDED(init))
        {
            CoUninitialize();
        }
        return opened;
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
        const std::filesystem::path solution = (GetGameDirectory() / ".." / "Canape.slnx").lexically_normal();
        std::error_code error;
        const std::filesystem::path absoluteFile = std::filesystem::absolute(file, error).lexically_normal();
        if (IsProcessRunning(L"devenv.exe"))
        {
            if (OpenInSolutionInstance(solution, absoluteFile))
            {
                return;
            }
            if (HWND window = FindSolutionWindow(solution.stem().wstring()))
            {
                CN_LOG_GAME("Editor", "Could not open the file in the running Visual Studio. "
                    "If it runs as administrator, run the editor at the same privilege level: ", ToUtf8(absoluteFile));
                if (IsIconic(window))
                {
                    ShowWindow(window, SW_RESTORE);
                }
                SetForegroundWindow(window);
                return;
            }
        }

        const std::wstring& devenv = FindVisualStudio();
        if (!devenv.empty())
        {
            DWORD exitCode = 0;
            if (RunProcess(Quote(devenv) + L" " + Quote(solution.wstring()), 0, exitCode, nullptr))
            {
                OpenWhenSolutionLoads(solution, absoluteFile);
                return;
            }
        }
        ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
