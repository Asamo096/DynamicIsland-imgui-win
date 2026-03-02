#pragma once

#include <windows.h>
#include <pdh.h>
#include <iphlpapi.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>

// 前向声明NVML类型 (动态加载)
typedef struct nvmlDevice_st* nvmlDevice_t;

// 数据结构定义 - 完全匹配README规格
struct CPUInfo {
    float usage_percent = 0.0f;
    float usage_per_core[32] = {};
    float frequency_ghz = 0.0f;
    uint64_t uptime_seconds = 0;
    std::chrono::steady_clock::time_point timestamp;
    int core_count = 0;
};

struct GPUInfo {
    float usage_percent = 0.0f;
    float memory_used_mb = 0.0f;
    float memory_total_mb = 0.0f;
    float temperature = 0.0f;
    std::string name = "Unknown";
    bool available = false;
    enum class Vendor { UNKNOWN, NVIDIA, AMD, INTEL } vendor = Vendor::UNKNOWN;
};

struct MemoryInfo {
    float usage_percent = 0.0f;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t available_bytes = 0;
};

struct BatteryInfo {
    int percent = 100;
    bool is_charging = false;
    bool is_plugged = false;
    int remaining_minutes = -1;
    std::string power_mode = "平衡";
};

struct DisplayInfo {
    int refresh_rate_hz = 60;
    int resolution_x = 1920;
    int resolution_y = 1080;
    float dpi_scale = 1.0f;
};

struct NetworkInfo {
    float download_speed_mbps = 0.0f;
    float upload_speed_mbps = 0.0f;
    uint64_t total_download_bytes = 0;
    uint64_t total_upload_bytes = 0;
    std::string adapter_name = "Unknown";
    bool is_connected = false;
};

// 系统信息监控管理器
class SysInfoManager {
public:
    static SysInfoManager& Instance();
    
    // 初始化和清理
    bool Initialize();
    void Shutdown();
    
    // 启动/停止监控线程
    void StartMonitoring();
    void StopMonitoring();
    
    // 数据获取接口 (线程安全)
    CPUInfo GetCPUInfo() const;
    GPUInfo GetGPUInfo() const;
    MemoryInfo GetMemoryInfo() const;
    BatteryInfo GetBatteryInfo() const;
    DisplayInfo GetDisplayInfo() const;
    NetworkInfo GetNetworkInfo() const;
    
    // 便捷接口
    float GetCpuUsage() const { return GetCPUInfo().usage_percent; }
    float GetMemUsage() const { return GetMemoryInfo().usage_percent; }
    float GetBatteryPercent() const { return (float)GetBatteryInfo().percent; }
    
    // 更新显示信息 (在WM_DISPLAYCHANGE时调用)
    void UpdateDisplayInfo();
    
private:
    SysInfoManager() = default;
    ~SysInfoManager() { Shutdown(); }
    SysInfoManager(const SysInfoManager&) = delete;
    SysInfoManager& operator=(const SysInfoManager&) = delete;
    
    // 监控线程函数
    void MonitoringLoop();
    
    // 各数据采集函数
    void UpdateCPU();
    void UpdateGPU();
    void UpdateMemory();
    void UpdateBattery();
    void UpdateNetwork();
    
    // GPU检测和初始化
    void DetectGPU();
    void InitNVIDIA();
    void InitAMD();
    void InitIntel();
    void CleanupGPU();
    
    // 数据
    mutable std::mutex dataMutex;
    CPUInfo cpuData;
    GPUInfo gpuData;
    MemoryInfo memData;
    BatteryInfo batteryData;
    DisplayInfo displayData;
    NetworkInfo networkData;
    
    // 线程控制
    std::atomic<bool> running{false};
    std::thread monitorThread;
    
    // CPU监控
    PDH_HQUERY cpuQuery = nullptr;
    PDH_HCOUNTER cpuTotalCounter = nullptr;
    std::vector<PDH_HCOUNTER> cpuCoreCounters;
    
    // 网络监控
    uint64_t prevDownloadBytes = 0;
    uint64_t prevUploadBytes = 0;
    std::chrono::steady_clock::time_point prevNetworkTime;
    
    // GPU监控
    bool gpuInitialized = false;
    
    // NVIDIA NVML (动态加载)
    HMODULE nvmlHandle = nullptr;
    nvmlDevice_t nvmlDevice = nullptr;
    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlShutdown_t)(void);
    typedef int (*nvmlDeviceGetCount_t)(unsigned int*);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, nvmlDevice_t*);
    typedef int (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t, void*);
    typedef int (*nvmlDeviceGetMemoryInfo_t)(nvmlDevice_t, void*);
    typedef int (*nvmlDeviceGetTemperature_t)(nvmlDevice_t, unsigned int, unsigned int*);
    typedef int (*nvmlDeviceGetName_t)(nvmlDevice_t, char*, unsigned int);
    nvmlInit_t nvmlInit = nullptr;
    nvmlShutdown_t nvmlShutdown = nullptr;
    nvmlDeviceGetCount_t nvmlDeviceGetCount = nullptr;
    nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex = nullptr;
    nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates = nullptr;
    nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo = nullptr;
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature = nullptr;
    nvmlDeviceGetName_t nvmlDeviceGetName = nullptr;
};

// 全局访问宏
#define g_sysinfo SysInfoManager::Instance()