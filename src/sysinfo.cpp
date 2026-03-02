#include "sysinfo.h"
#include <psapi.h>
#include <dxgi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <cmath>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "wbemuuid.lib")

// NVML常量
#define NVML_SUCCESS 0
#define NVML_TEMPERATURE_GPU 0

struct nvmlUtilization_st {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_st {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

SysInfoManager& SysInfoManager::Instance() {
    static SysInfoManager instance;
    return instance;
}

bool SysInfoManager::Initialize() {
    // 初始化PDH用于CPU监控
    if (PdhOpenQueryA(nullptr, 0, &cpuQuery) != ERROR_SUCCESS) {
        cpuQuery = nullptr;
    }
    
    if (cpuQuery) {
        // 添加总CPU使用率计数器
        PdhAddCounterA(cpuQuery, "\\Processor(_Total)\\% Processor Time", 0, &cpuTotalCounter);
        
        // 获取CPU核心数并添加各核心计数器
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cpuData.core_count = si.dwNumberOfProcessors;
        
        for (int i = 0; i < cpuData.core_count && i < 32; i++) {
            PDH_HCOUNTER counter;
            char path[256];
            sprintf_s(path, "\\Processor(%d)\\%% Processor Time", i);
            if (PdhAddCounterA(cpuQuery, path, 0, &counter) == ERROR_SUCCESS) {
                cpuCoreCounters.push_back(counter);
            }
        }
        
        // 首次查询需要两次调用才能获得有效数据
        PdhCollectQueryData(cpuQuery);
    }
    
    // 检测GPU
    DetectGPU();
    
    // 初始化显示信息
    UpdateDisplayInfo();
    
    // 初始化网络时间戳
    prevNetworkTime = std::chrono::steady_clock::now();
    
    return true;
}

void SysInfoManager::Shutdown() {
    StopMonitoring();
    CleanupGPU();
    
    if (cpuQuery) {
        PdhCloseQuery(cpuQuery);
        cpuQuery = nullptr;
    }
}

void SysInfoManager::StartMonitoring() {
    if (running.exchange(true)) return;
    monitorThread = std::thread(&SysInfoManager::MonitoringLoop, this);
}

void SysInfoManager::StopMonitoring() {
    running = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void SysInfoManager::MonitoringLoop() {
    while (running) {
        UpdateCPU();
        UpdateGPU();
        UpdateMemory();
        UpdateBattery();
        UpdateNetwork();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void SysInfoManager::UpdateCPU() {
    if (cpuQuery) {
        PdhCollectQueryData(cpuQuery);
        
        PDH_FMT_COUNTERVALUE value;
        if (cpuTotalCounter && PdhGetFormattedCounterValue(cpuTotalCounter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
            std::lock_guard<std::mutex> lock(dataMutex);
            cpuData.usage_percent = static_cast<float>(value.doubleValue);
            cpuData.timestamp = std::chrono::steady_clock::now();
        }
        
        // 各核心使用率
        for (size_t i = 0; i < cpuCoreCounters.size() && i < 32; i++) {
            if (PdhGetFormattedCounterValue(cpuCoreCounters[i], PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
                std::lock_guard<std::mutex> lock(dataMutex);
                cpuData.usage_per_core[i] = static_cast<float>(value.doubleValue);
            }
        }
    }
    
    // 获取系统运行时间
    ULONGLONG uptime = GetTickCount64() / 1000;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        cpuData.uptime_seconds = uptime;
    }
}

void SysInfoManager::DetectGPU() {
    // 尝试检测NVIDIA GPU
    nvmlHandle = LoadLibraryA("nvml.dll");
    if (nvmlHandle) {
        nvmlInit = (nvmlInit_t)GetProcAddress(nvmlHandle, "nvmlInit_v2");
        if (!nvmlInit) nvmlInit = (nvmlInit_t)GetProcAddress(nvmlHandle, "nvmlInit");
        nvmlShutdown = (nvmlShutdown_t)GetProcAddress(nvmlHandle, "nvmlShutdown");
        nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetCount");
        nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetHandleByIndex_v2");
        if (!nvmlDeviceGetHandleByIndex) nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetHandleByIndex");
        nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetUtilizationRates");
        nvmlDeviceGetMemoryInfo = (nvmlDeviceGetMemoryInfo_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetMemoryInfo");
        nvmlDeviceGetTemperature = (nvmlDeviceGetTemperature_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetTemperature");
        nvmlDeviceGetName = (nvmlDeviceGetName_t)GetProcAddress(nvmlHandle, "nvmlDeviceGetName");
        
        if (nvmlInit && nvmlInit() == NVML_SUCCESS) {
            unsigned int count = 0;
            if (nvmlDeviceGetCount && nvmlDeviceGetCount(&count) == NVML_SUCCESS && count > 0) {
                if (nvmlDeviceGetHandleByIndex && nvmlDeviceGetHandleByIndex(0, &nvmlDevice) == NVML_SUCCESS) {
                    gpuData.vendor = GPUInfo::Vendor::NVIDIA;
                    gpuData.available = true;
                    gpuInitialized = true;
                    
                    // 获取GPU名称
                    if (nvmlDeviceGetName) {
                        char name[256] = {};
                        if (nvmlDeviceGetName(nvmlDevice, name, sizeof(name)) == NVML_SUCCESS) {
                            gpuData.name = name;
                        }
                    }
                    return;
                }
            }
        }
    }
    
    // 尝试使用DXGI检测GPU (通用方法)
    IDXGIFactory* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        IDXGIAdapter* pAdapter = nullptr;
        if (SUCCEEDED(pFactory->EnumAdapters(0, &pAdapter))) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                char name[256];
                WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, name, 256, nullptr, nullptr);
                gpuData.name = name;
                gpuData.memory_total_mb = desc.DedicatedVideoMemory / (1024.0f * 1024.0f);
                gpuData.available = true;
                
                // 检测厂商
                if (wcsstr(desc.Description, L"NVIDIA") || wcsstr(desc.Description, L"GeForce") || wcsstr(desc.Description, L"RTX")) {
                    gpuData.vendor = GPUInfo::Vendor::NVIDIA;
                } else if (wcsstr(desc.Description, L"AMD") || wcsstr(desc.Description, L"Radeon")) {
                    gpuData.vendor = GPUInfo::Vendor::AMD;
                } else if (wcsstr(desc.Description, L"Intel")) {
                    gpuData.vendor = GPUInfo::Vendor::INTEL;
                }
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }
}

void SysInfoManager::UpdateGPU() {
    if (!gpuData.available) return;
    
    std::lock_guard<std::mutex> lock(dataMutex);
    
    if (gpuData.vendor == GPUInfo::Vendor::NVIDIA && nvmlDevice && nvmlDeviceGetUtilizationRates) {
        nvmlUtilization_st util;
        if (nvmlDeviceGetUtilizationRates(nvmlDevice, &util) == NVML_SUCCESS) {
            gpuData.usage_percent = static_cast<float>(util.gpu);
        }
        
        if (nvmlDeviceGetMemoryInfo) {
            nvmlMemory_st mem;
            if (nvmlDeviceGetMemoryInfo(nvmlDevice, &mem) == NVML_SUCCESS) {
                gpuData.memory_used_mb = mem.used / (1024.0f * 1024.0f);
                gpuData.memory_total_mb = mem.total / (1024.0f * 1024.0f);
            }
        }
        
        if (nvmlDeviceGetTemperature) {
            unsigned int temp;
            if (nvmlDeviceGetTemperature(nvmlDevice, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
                gpuData.temperature = static_cast<float>(temp);
            }
        }
    }
    // AMD和Intel GPU监控可以通过WMI实现，这里简化处理
}

void SysInfoManager::UpdateMemory() {
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        std::lock_guard<std::mutex> lock(dataMutex);
        memData.total_bytes = mem.ullTotalPhys;
        memData.available_bytes = mem.ullAvailPhys;
        memData.used_bytes = mem.ullTotalPhys - mem.ullAvailPhys;
        memData.usage_percent = 100.0f * memData.used_bytes / memData.total_bytes;
    }
}

void SysInfoManager::UpdateBattery() {
    SYSTEM_POWER_STATUS pwr;
    if (GetSystemPowerStatus(&pwr)) {
        std::lock_guard<std::mutex> lock(dataMutex);
        batteryData.percent = (pwr.BatteryLifePercent == 255) ? 100 : pwr.BatteryLifePercent;
        batteryData.is_charging = (pwr.BatteryFlag & 8) != 0;
        batteryData.is_plugged = (pwr.ACLineStatus == 1);
        
        if (pwr.BatteryLifeTime != -1) {
            batteryData.remaining_minutes = pwr.BatteryLifeTime / 60;
        } else {
            batteryData.remaining_minutes = -1;
        }
    }
}

void SysInfoManager::UpdateDisplayInfo() {
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    
    if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        std::lock_guard<std::mutex> lock(dataMutex);
        displayData.refresh_rate_hz = dm.dmDisplayFrequency;
        displayData.resolution_x = dm.dmPelsWidth;
        displayData.resolution_y = dm.dmPelsHeight;
    }
    
    // 获取DPI缩放
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        std::lock_guard<std::mutex> lock(dataMutex);
        displayData.dpi_scale = dpi / 96.0f;
    }
}

void SysInfoManager::UpdateNetwork() {
    // 使用GetIfTable API (兼容旧版Windows)
    ULONG ulSize = 0;
    DWORD dwResult = GetIfTable(nullptr, &ulSize, FALSE);
    if (dwResult != ERROR_INSUFFICIENT_BUFFER) return;
    
    MIB_IFTABLE* pIfTable = (MIB_IFTABLE*)malloc(ulSize);
    if (!pIfTable) return;
    
    dwResult = GetIfTable(pIfTable, &ulSize, FALSE);
    if (dwResult == NO_ERROR) {
        uint64_t totalDownload = 0;
        uint64_t totalUpload = 0;
        bool found = false;
        
        for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
            MIB_IFROW& row = pIfTable->table[i];
            // 只统计已连接的非虚拟网卡
            if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL && 
                (row.dwType == IF_TYPE_ETHERNET_CSMACD || row.dwType == IF_TYPE_IEEE80211)) {
                totalDownload += row.dwInOctets;
                totalUpload += row.dwOutOctets;
                found = true;
                
                if (networkData.adapter_name == "Unknown") {
                    char name[256] = {};
                    WideCharToMultiByte(CP_ACP, 0, row.wszName, -1, name, 256, nullptr, nullptr);
                    networkData.adapter_name = name;
                }
            }
        }
        
        if (found) {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - prevNetworkTime).count();
            
            if (duration > 0 && prevDownloadBytes > 0 && prevUploadBytes > 0) {
                float seconds = duration / 1000.0f;
                uint64_t downloadDiff = totalDownload - prevDownloadBytes;
                uint64_t uploadDiff = totalUpload - prevUploadBytes;
                
                std::lock_guard<std::mutex> lock(dataMutex);
                networkData.download_speed_mbps = (downloadDiff * 8.0f) / (seconds * 1000000.0f);
                networkData.upload_speed_mbps = (uploadDiff * 8.0f) / (seconds * 1000000.0f);
                networkData.total_download_bytes = totalDownload;
                networkData.total_upload_bytes = totalUpload;
                networkData.is_connected = true;
            }
            
            prevDownloadBytes = totalDownload;
            prevUploadBytes = totalUpload;
            prevNetworkTime = now;
        } else {
            std::lock_guard<std::mutex> lock(dataMutex);
            networkData.is_connected = false;
        }
    }
    
    free(pIfTable);
}

void SysInfoManager::CleanupGPU() {
    if (nvmlHandle) {
        if (nvmlShutdown) nvmlShutdown();
        FreeLibrary(nvmlHandle);
        nvmlHandle = nullptr;
        nvmlDevice = nullptr;
    }
}

// 线程安全的数据获取接口
CPUInfo SysInfoManager::GetCPUInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return cpuData;
}

GPUInfo SysInfoManager::GetGPUInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return gpuData;
}

MemoryInfo SysInfoManager::GetMemoryInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return memData;
}

BatteryInfo SysInfoManager::GetBatteryInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return batteryData;
}

DisplayInfo SysInfoManager::GetDisplayInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return displayData;
}

NetworkInfo SysInfoManager::GetNetworkInfo() const {
    std::lock_guard<std::mutex> lock(dataMutex);
    return networkData;
}
