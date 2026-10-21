#ifndef __CorridorKey_IpcClient_h__
#define __CorridorKey_IpcClient_h__

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace corridor_key_avx {

enum class MessageType : uint32_t {
    Ping = 0,
    ProcessFrame = 1,
    Shutdown = 2,
    LoadModel = 3,
    AnalyzeClip = 4,
    GetCachedFrame = 5,
    Pong = 100,
    FrameDone = 101,
    Error = 102,
    ModelLoaded = 103,
    Status = 104,
    AnalyzeDone = 105,
};

struct ProcessParams {
    uint32_t width = 0;
    uint32_t height = 0;
    int mode = 0;              // 0=Auto, 1=External hint
    int input_colorspace = 0;  // 0=sRGB, 1=Linear
    float despill_strength = 0.5f;
    float hint_quality = 0.95f;
    bool carry_hint = true;
    int output_mode = 0;       // 0=Processed, 1=Matte, 2=Foreground, 3=Checker
    int time_aware = 0;        // 0=Off, 1=Analyze
    int temporal_window = 16;  // hann_chunk when time_aware (1–80)
    bool has_alpha_hint = false;
    int32_t frame_number = 0;
};

class IpcClient {
public:
    IpcClient();
    ~IpcClient();

    bool EnsureWorkerRunning();
    bool Ping(uint32_t timeoutMs = 5000);
    bool LoadModel(uint32_t timeoutMs = 600000);
    bool ProcessFrame(const ProcessParams& params,
                      const float* inputRgba,
                      const float* hintRgba,
                      float* outputRgba,
                      uint32_t timeoutMs = 600000);
    bool AnalyzeClipStart(const ProcessParams& params, uint32_t timeoutMs = 5000);
    bool GetCachedFrame(int32_t frameNumber, uint32_t width, uint32_t height,
                        float* outputRgba, uint32_t timeoutMs = 60000);
    void Shutdown();

    std::string LastError() const { return lastError_; }
    std::string InstallRoot() const { return installRoot_; }

private:
    bool ConnectPipe(uint32_t timeoutMs);
    bool SendMessage(MessageType type, const std::string& jsonPayload);
    bool RecvMessage(MessageType& type, std::string& jsonPayload, uint32_t timeoutMs);
    bool EnsureSharedMemory(uint32_t width, uint32_t height);
    void CloseSharedMemory();
    bool ResolveInstallRoot();
    bool SpawnWorker();

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE shmInput_ = nullptr;
    HANDLE shmOutput_ = nullptr;
    HANDLE shmHint_ = nullptr;
    void* viewInput_ = nullptr;
    void* viewOutput_ = nullptr;
    void* viewHint_ = nullptr;
    size_t shmBytes_ = 0;
    PROCESS_INFORMATION workerPi_{};
    bool workerStarted_ = false;
    std::string installRoot_;
    std::string lastError_;
};

}  // namespace corridor_key_avx

#endif
