#include "IpcClient.h"

#include <fstream>
#include <sstream>
#include <vector>

namespace corridor_key_avx {

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\CorridorKeyAVX";
constexpr wchar_t kShmInput[] = L"CorridorKeyAVX_Input";
constexpr wchar_t kShmOutput[] = L"CorridorKeyAVX_Output";
constexpr wchar_t kShmHint[] = L"CorridorKeyAVX_AlphaHint";
constexpr uint32_t kMaxDim = 4096;
constexpr uint32_t kHeaderBytes = 16;
constexpr uint32_t kChannels = 4;

size_t FrameBytes(uint32_t w, uint32_t h) {
    return static_cast<size_t>(kHeaderBytes) +
           static_cast<size_t>(w) * static_cast<size_t>(h) * kChannels * sizeof(float);
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], n, nullptr, nullptr);
    return out;
}

void WriteHeader(void* base, uint32_t w, uint32_t h) {
    uint32_t* p = static_cast<uint32_t*>(base);
    p[0] = w;
    p[1] = h;
    p[2] = kChannels;
    p[3] = 0;
}

}  // namespace

IpcClient::IpcClient() {
    ZeroMemory(&workerPi_, sizeof(workerPi_));
    ResolveInstallRoot();
}

IpcClient::~IpcClient() {
    Shutdown();
    CloseSharedMemory();
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool IpcClient::ResolveInstallRoot() {
    // 1) Env override for developers
    wchar_t envBuf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"CORRIDORKEY_AVX_ROOT", envBuf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        installRoot_ = Narrow(envBuf);
        return true;
    }

    // 2) Installer registry key
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\CorridorKeyAVX", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t val[MAX_PATH] = {};
        DWORD typ = 0;
        DWORD cb = sizeof(val);
        if (RegQueryValueExW(key, L"InstallRoot", nullptr, &typ, reinterpret_cast<LPBYTE>(val), &cb) == ERROR_SUCCESS
            && typ == REG_SZ) {
            RegCloseKey(key);
            if (GetFileAttributesW(val) != INVALID_FILE_ATTRIBUTES) {
                installRoot_ = Narrow(val);
                return true;
            }
        } else {
            RegCloseKey(key);
        }
    }

    // 3) Default Program Files location from installer
    wchar_t pf[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH) > 0) {
        std::wstring candidate = std::wstring(pf) + L"\\CorridorKeyAVX";
        std::wstring marker = candidate + L"\\runtime\\server.py";
        if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
            installRoot_ = Narrow(candidate);
            return true;
        }
        marker = candidate + L"\\python\\python.exe";
        if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
            installRoot_ = Narrow(candidate);
            return true;
        }
    }

    // 4) Dev fallback: this workspace
    const wchar_t* dev = L"E:\\personal-workspace\\corridor-key-v2-avid";
    if (GetFileAttributesW(dev) != INVALID_FILE_ATTRIBUTES) {
        installRoot_ = Narrow(dev);
        return true;
    }

    lastError_ = "CorridorKeyAVX install folder not found. Re-run the installer.";
    return false;
}

bool IpcClient::SpawnWorker() {
    if (workerStarted_) {
        DWORD code = 0;
        if (workerPi_.hProcess && GetExitCodeProcess(workerPi_.hProcess, &code) && code == STILL_ACTIVE) {
            return true;
        }
        workerStarted_ = false;
    }

    if (installRoot_.empty() && !ResolveInstallRoot()) {
        return false;
    }

    // Drop orphaned CorridorKey pipe servers from a previous Media Composer session.
    {
        STARTUPINFOW ksi{};
        ksi.cb = sizeof(ksi);
        ksi.dwFlags = STARTF_USESHOWWINDOW;
        ksi.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION kpi{};
        wchar_t killCmd[] =
            L"powershell.exe -NoProfile -WindowStyle Hidden -Command "
            L"\"Get-CimInstance Win32_Process | Where-Object { "
            L"$_.CommandLine -like '*server.py*' -and $_.CommandLine -like '*CorridorKey*' "
            L"} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }\"";
        if (CreateProcessW(nullptr, killCmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &ksi, &kpi)) {
            WaitForSingleObject(kpi.hProcess, 5000);
            CloseHandle(kpi.hThread);
            CloseHandle(kpi.hProcess);
        }
        Sleep(300);
    }

    std::wstring root = Widen(installRoot_);
    std::wstring pythonExe = root + L"\\python\\python.exe";
    std::wstring serverPy = root + L"\\runtime\\server.py";
    std::wstring ckRoot = root + L"\\CorridorKey2.0";

    // Dev layout: repo root with CorridorKey2.0 + runtime/
    if (GetFileAttributesW(pythonExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        pythonExe = root + L"\\CorridorKey2.0\\.venv\\Scripts\\python.exe";
        serverPy = root + L"\\runtime\\server.py";
        ckRoot = root + L"\\CorridorKey2.0";
    }
    if (GetFileAttributesW(pythonExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Fall back to system python only for developer machines
        pythonExe = L"python";
    }
    if (GetFileAttributesW(serverPy.c_str()) == INVALID_FILE_ATTRIBUTES) {
        lastError_ = "runtime\\server.py missing under " + installRoot_;
        return false;
    }

    // Program Files is not writable for the worker; log next to effect.log.
    wchar_t localAppData[MAX_PATH] = {};
    std::wstring logDir;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        logDir = std::wstring(localAppData) + L"\\CorridorKeyAVX\\logs";
    } else {
        logDir = root + L"\\logs";
    }
    CreateDirectoryW((std::wstring(localAppData) + L"\\CorridorKeyAVX").c_str(), nullptr);
    CreateDirectoryW(logDir.c_str(), nullptr);
    std::wstring logFile = logDir + L"\\worker.log";
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hLog = CreateFileW(
        logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    std::wstring cmd = L"\"" + pythonExe + L"\" \"" + serverPy + L"\" --ck-root \"" + ckRoot + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    if (hLog != INVALID_HANDLE_VALUE) {
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    ZeroMemory(&workerPi_, sizeof(workerPi_));

    std::wstring workDir = root;
    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,  // inherit log handles
        CREATE_NO_WINDOW,
        nullptr,
        workDir.c_str(),
        &si,
        &workerPi_);
    if (hLog != INVALID_HANDLE_VALUE)
        CloseHandle(hLog);
    if (!ok) {
        std::ostringstream oss;
        oss << "Failed to start CorridorKey worker (err=" << GetLastError() << ")";
        lastError_ = oss.str();
        return false;
    }
    workerStarted_ = true;
    // Give the pipe a moment to appear
    Sleep(500);
    return true;
}

bool IpcClient::ConnectPipe(uint32_t timeoutMs) {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        return true;
    }
    const DWORD start = GetTickCount();
    while (true) {
        pipe_ = CreateFileW(
            kPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);
            return true;
        }
        if (GetLastError() != ERROR_PIPE_BUSY && GetLastError() != ERROR_FILE_NOT_FOUND) {
            lastError_ = "Cannot open CorridorKeyAVX pipe";
            return false;
        }
        if (GetTickCount() - start > timeoutMs) {
            lastError_ = "Timed out waiting for CorridorKeyAVX worker pipe";
            return false;
        }
        WaitNamedPipeW(kPipeName, 500);
    }
}

bool IpcClient::EnsureWorkerRunning() {
    // If a prior exchange broke the pipe, force reconnect/respawn.
    if (pipe_ == INVALID_HANDLE_VALUE) {
        workerStarted_ = false;
    }
    if (!SpawnWorker()) return false;
    if (!ConnectPipe(30000)) return false;
    if (!Ping(5000)) {
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        workerStarted_ = false;
        // One retry: kill stale servers and spawn fresh
        if (!SpawnWorker()) return false;
        if (!ConnectPipe(30000)) return false;
        return Ping(5000);
    }
    return true;
}

bool IpcClient::SendMessage(MessageType type, const std::string& jsonPayload) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;
    uint32_t t = static_cast<uint32_t>(type);
    uint32_t len = static_cast<uint32_t>(jsonPayload.size());
    std::vector<char> buf(8 + jsonPayload.size());
    memcpy(buf.data(), &t, 4);
    memcpy(buf.data() + 4, &len, 4);
    if (!jsonPayload.empty()) {
        memcpy(buf.data() + 8, jsonPayload.data(), jsonPayload.size());
    }
    DWORD written = 0;
    // One WriteFile = one MESSAGE-mode pipe message (must match Python).
    if (!WriteFile(pipe_, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr)
        || written != buf.size()) {
        lastError_ = "WriteFile to pipe failed";
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

bool IpcClient::RecvMessage(MessageType& type, std::string& jsonPayload, uint32_t timeoutMs) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;
    COMMTIMEOUTS to{};
    to.ReadTotalTimeoutConstant = timeoutMs;
    SetCommTimeouts(pipe_, &to);

    // MESSAGE-mode pipes deliver one whole message per ReadFile. Reading only 8
    // bytes fails with ERROR_MORE_DATA when the JSON payload is non-empty.
    std::vector<char> buf(1024 * 1024);
    DWORD read = 0;
    BOOL ok = ReadFile(pipe_, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    if (!ok) {
        const DWORD err = GetLastError();
        if (err == ERROR_MORE_DATA) {
            lastError_ = "Pipe message larger than 1MB buffer";
        } else {
            lastError_ = "Failed to read pipe message";
        }
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    if (read < 8) {
        lastError_ = "Failed to read pipe header";
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    uint32_t t = 0, len = 0;
    memcpy(&t, buf.data(), 4);
    memcpy(&len, buf.data() + 4, 4);
    if (8u + len > read) {
        lastError_ = "Pipe message truncated";
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
        return false;
    }
    type = static_cast<MessageType>(t);
    jsonPayload.clear();
    if (len > 0) {
        jsonPayload.assign(buf.data() + 8, buf.data() + 8 + len);
    }
    return true;
}

bool IpcClient::EnsureSharedMemory(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width > kMaxDim || height > kMaxDim) {
        lastError_ = "Invalid frame size";
        return false;
    }
    size_t bytes = FrameBytes(width, height);
    if (viewInput_ && shmBytes_ >= bytes) {
        return true;
    }
    CloseSharedMemory();
    shmBytes_ = bytes;

    auto create = [&](const wchar_t* name, HANDLE& h, void*& view) -> bool {
        h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                               static_cast<DWORD>((bytes >> 32) & 0xffffffffu),
                               static_cast<DWORD>(bytes & 0xffffffffu),
                               name);
        if (!h) {
            lastError_ = "CreateFileMapping failed";
            return false;
        }
        view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (!view) {
            lastError_ = "MapViewOfFile failed";
            return false;
        }
        return true;
    };

    if (!create(kShmInput, shmInput_, viewInput_)) return false;
    if (!create(kShmOutput, shmOutput_, viewOutput_)) return false;
    if (!create(kShmHint, shmHint_, viewHint_)) return false;
    return true;
}

void IpcClient::CloseSharedMemory() {
    if (viewInput_) { UnmapViewOfFile(viewInput_); viewInput_ = nullptr; }
    if (viewOutput_) { UnmapViewOfFile(viewOutput_); viewOutput_ = nullptr; }
    if (viewHint_) { UnmapViewOfFile(viewHint_); viewHint_ = nullptr; }
    if (shmInput_) { CloseHandle(shmInput_); shmInput_ = nullptr; }
    if (shmOutput_) { CloseHandle(shmOutput_); shmOutput_ = nullptr; }
    if (shmHint_) { CloseHandle(shmHint_); shmHint_ = nullptr; }
    shmBytes_ = 0;
}

bool IpcClient::Ping(uint32_t timeoutMs) {
    if (!ConnectPipe(timeoutMs)) return false;
    if (!SendMessage(MessageType::Ping, "{}")) return false;
    MessageType t;
    std::string payload;
    if (!RecvMessage(t, payload, timeoutMs)) return false;
    if (t != MessageType::Pong) {
        lastError_ = "Unexpected reply to PING";
        return false;
    }
    return true;
}

bool IpcClient::LoadModel(uint32_t timeoutMs) {
    if (!EnsureWorkerRunning()) return false;
    if (!SendMessage(MessageType::LoadModel, "{}")) return false;
    MessageType t;
    std::string payload;
    if (!RecvMessage(t, payload, timeoutMs)) return false;
    if (t == MessageType::Error) {
        lastError_ = payload.empty() ? "LOAD_MODEL failed" : payload;
        return false;
    }
    if (t != MessageType::ModelLoaded) {
        lastError_ = "Unexpected reply to LOAD_MODEL";
        return false;
    }
    return true;
}

bool IpcClient::ProcessFrame(const ProcessParams& params,
                             const float* inputRgba,
                             const float* hintRgba,
                             float* outputRgba,
                             uint32_t timeoutMs) {
    if (!EnsureWorkerRunning()) return false;
    if (!EnsureSharedMemory(params.width, params.height)) return false;

    WriteHeader(viewInput_, params.width, params.height);
    float* inPix = reinterpret_cast<float*>(static_cast<char*>(viewInput_) + kHeaderBytes);
    const size_t pixCount = static_cast<size_t>(params.width) * params.height * kChannels;
    memcpy(inPix, inputRgba, pixCount * sizeof(float));

    if (params.has_alpha_hint && hintRgba) {
        WriteHeader(viewHint_, params.width, params.height);
        float* hintPix = reinterpret_cast<float*>(static_cast<char*>(viewHint_) + kHeaderBytes);
        memcpy(hintPix, hintRgba, pixCount * sizeof(float));
    }

    std::ostringstream json;
    json << "{"
         << "\"width\":" << params.width << ","
         << "\"height\":" << params.height << ","
         << "\"mode\":" << params.mode << ","
         << "\"input_colorspace\":" << params.input_colorspace << ","
         << "\"despill_strength\":" << params.despill_strength << ","
         << "\"hint_quality\":" << params.hint_quality << ","
         << "\"carry_hint\":" << (params.carry_hint ? "true" : "false") << ","
         << "\"output_mode\":" << params.output_mode << ","
         << "\"time_aware\":" << params.time_aware << ","
         << "\"temporal_window\":" << params.temporal_window << ","
         << "\"has_alpha_hint\":" << (params.has_alpha_hint ? "true" : "false") << ","
         << "\"frame_number\":" << params.frame_number
         << "}";

    if (!SendMessage(MessageType::ProcessFrame, json.str())) return false;

    MessageType t;
    std::string payload;
    if (!RecvMessage(t, payload, timeoutMs)) return false;
    if (t == MessageType::Error) {
        lastError_ = payload.empty() ? "PROCESS_FRAME failed" : payload;
        return false;
    }
    if (t != MessageType::FrameDone) {
        lastError_ = "Unexpected reply to PROCESS_FRAME";
        return false;
    }

    float* outPix = reinterpret_cast<float*>(static_cast<char*>(viewOutput_) + kHeaderBytes);
    memcpy(outputRgba, outPix, pixCount * sizeof(float));
    return true;
}

bool IpcClient::AnalyzeClipStart(const ProcessParams& params, uint32_t timeoutMs) {
    if (!EnsureWorkerRunning()) return false;
    std::ostringstream json;
    json << "{"
         << "\"width\":" << params.width << ","
         << "\"height\":" << params.height << ","
         << "\"mode\":" << params.mode << ","
         << "\"input_colorspace\":" << params.input_colorspace << ","
         << "\"despill_strength\":" << params.despill_strength << ","
         << "\"hint_quality\":" << params.hint_quality << ","
         << "\"carry_hint\":" << (params.carry_hint ? "true" : "false") << ","
         << "\"output_mode\":" << params.output_mode << ","
         << "\"temporal_window\":" << params.temporal_window
         << "}";
    if (!SendMessage(MessageType::AnalyzeClip, json.str())) return false;
    MessageType t;
    std::string payload;
    if (!RecvMessage(t, payload, timeoutMs)) return false;
    if (t == MessageType::Error) {
        lastError_ = payload;
        return false;
    }
    return t == MessageType::Status || t == MessageType::AnalyzeDone;
}

bool IpcClient::GetCachedFrame(int32_t frameNumber, uint32_t width, uint32_t height,
                               float* outputRgba, uint32_t timeoutMs) {
    if (!EnsureWorkerRunning()) return false;
    if (!EnsureSharedMemory(width, height)) return false;
    std::ostringstream json;
    json << "{\"frame_number\":" << frameNumber
         << ",\"width\":" << width
         << ",\"height\":" << height << "}";
    if (!SendMessage(MessageType::GetCachedFrame, json.str())) return false;
    MessageType t;
    std::string payload;
    if (!RecvMessage(t, payload, timeoutMs)) return false;
    if (t == MessageType::Error) {
        lastError_ = payload;
        return false;
    }
    if (t != MessageType::FrameDone) return false;
    float* outPix = reinterpret_cast<float*>(static_cast<char*>(viewOutput_) + kHeaderBytes);
    const size_t pixCount = static_cast<size_t>(width) * height * kChannels;
    memcpy(outputRgba, outPix, pixCount * sizeof(float));
    return true;
}

void IpcClient::Shutdown() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        SendMessage(MessageType::Shutdown, "{}");
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    if (workerPi_.hProcess) {
        WaitForSingleObject(workerPi_.hProcess, 2000);
        TerminateProcess(workerPi_.hProcess, 0);
        CloseHandle(workerPi_.hProcess);
        CloseHandle(workerPi_.hThread);
        ZeroMemory(&workerPi_, sizeof(workerPi_));
    }
    workerStarted_ = false;
}

}  // namespace corridor_key_avx
