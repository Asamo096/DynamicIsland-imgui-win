#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <chrono>
#include <stdio.h>
#include <shlobj.h>
#include <ole2.h>
#include <oleidl.h>
#include <algorithm>

// DWM attribute not defined in older SDKs
#ifndef DWMWA_USE_HOSTBACKDROPBRUSH
#define DWMWA_USE_HOSTBACKDROPBRUSH 38
#endif

#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// 文件中转站功能开关 (0=禁用, 1=启用)
#define USE_FILE_TRANSFER 0

// 项目模块
#include "config.h"
#include "sysinfo.h"
#if USE_FILE_TRANSFER
#include "transferstation.h"
#endif

#include "trayicon.h"
#include "scheduler.h"

// 日志级别控制
enum LogLevel {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_ERROR
};

static LogLevel g_logLevel = LOG_LEVEL_DEBUG; // 默认输出所有日志

// 简单日志宏
#define LOG(level, msg, ...) do { \
    if (level >= g_logLevel) { \
        FILE* f = fopen("dynamicisland.log", "a"); \
        if(f) { \
            const char* levelStr = "[DEBUG]"; \
            if (level == LOG_LEVEL_INFO) levelStr = "[INFO]"; \
            else if (level == LOG_LEVEL_ERROR) levelStr = "[ERROR]"; \
            fprintf(f, "%s [%s:%d] " msg "\n", levelStr, __FILE__, __LINE__, ##__VA_ARGS__); \
            fclose(f); \
        } \
    } \
} while(0)

#define LOG_DEBUG(msg, ...) LOG(LOG_LEVEL_DEBUG, msg, ##__VA_ARGS__)
#define LOG_INFO(msg, ...) LOG(LOG_LEVEL_INFO, msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) LOG(LOG_LEVEL_ERROR, msg, ##__VA_ARGS__)

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

// 全局变量
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
// 窗口相关
static HWND g_hwnd = nullptr;
static bool g_running = true;
static bool g_windowVisible = true;
static bool g_islandVisible = true; // 控制灵动岛的可见性
static bool g_islandExpanded = false; // 控制灵动岛是否展开

// 拖放相关
// 文件拖动相关
#if USE_FILE_TRANSFER
static size_t g_dragFileIndex = -1; // 当前拖动的文件索引
#endif

// 文件预览相关
#if USE_FILE_TRANSFER
static size_t g_selectedFileIndex = -1; // 当前选中的文件索引
static bool g_showPreview = false; // 是否显示预览窗口
#endif

// 设置窗口
static bool g_showSettings = false; // 是否显示设置窗口

// IDropSource 实现
class CDropSource : public IDropSource {
private:
    ULONG m_refCount;

public:
    CDropSource() : m_refCount(1) {}
    ~CDropSource() {}

    // IUnknown 方法
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppvObject = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() {
        return InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() {
        ULONG ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) {
            delete this;
        }
        return ref;
    }

    // IDropSource 方法
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) {
        if (fEscapePressed) {
            return DRAGDROP_S_CANCEL;
        }
        if (!(grfKeyState & MK_LBUTTON)) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// 创建文件拖放数据对象
HRESULT CreateFileDropDataObject(const std::vector<std::wstring>& filePaths, IDataObject** ppDataObject) {
    HRESULT hr;
    
    size_t fileCount = filePaths.size();
    if (fileCount == 0) {
        return E_INVALIDARG;
    }
    
    // 创建 ITEMIDLIST 数组
    std::vector<LPITEMIDLIST> pidls;
    pidls.reserve(fileCount);
    
    for (const auto& filePath : filePaths) {
        LPITEMIDLIST pidl = ILCreateFromPathW(filePath.c_str());
        if (pidl) {
            pidls.push_back(pidl);
        } else {
            // 清理已创建的 pidl
            for (auto p : pidls) {
                ILFree(p);
            }
            return E_FAIL;
        }
    }
    
    // 使用 SHCreateDataObject 创建数据对象
    hr = SHCreateDataObject(
        nullptr, // 父窗口
        (UINT)pidls.size(),
        const_cast<const ITEMIDLIST**>(pidls.data()),
        nullptr, // 格式
        IID_IDataObject,
        (void**)ppDataObject
    );
    
    // 清理 pidl
    for (auto p : pidls) {
        ILFree(p);
    }
    
    return hr;
}

// 托盘消息
static const UINT WM_TRAYICON = WM_APP + 1;

// 前向声明
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 更新窗口位置和大小
void UpdateWindowGeometry() {
    if (!g_hwnd) return;

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    BOOL useBackdrop = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &useBackdrop, sizeof(useBackdrop));

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);
}

// 创建设备D3D
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { 
        D3D_FEATURE_LEVEL_11_0, 
        D3D_FEATURE_LEVEL_10_0 
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevelArray, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );

    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, &rtvDesc, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { 
        g_mainRenderTargetView->Release(); 
        g_mainRenderTargetView = nullptr; 
    }
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// 窗口过程
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    // ESC 键关闭设置窗口
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && g_showSettings) {
        g_showSettings = false;
        return 0;
    }

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), 
                                        DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        // 窗口大小变化时更新
        UpdateWindowGeometry();
        return 0;
        
    case WM_NCHITTEST:
        // 允许拖拽窗口，同时在非显示区域透明
        {
            // 获取鼠标坐标
            POINT pt;
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            
            // 将屏幕坐标转换为窗口坐标
            ::ScreenToClient(hWnd, &pt);
            
            // 检查鼠标是否在窗口的很小一部分区域内（例如，窗口的顶部中心区域）
            // 这样只有在这个小区域内才能拖拽窗口，其他区域都透明
            RECT rcClient;
            ::GetClientRect(hWnd, &rcClient);
            
            // 定义一个小的拖拽区域
            int dragAreaWidth = 100;
            int dragAreaHeight = 20;
            int dragAreaX = (rcClient.right - rcClient.left - dragAreaWidth) / 2;
            int dragAreaY = 10;
            
            RECT dragRect;
            dragRect.left = dragAreaX;
            dragRect.top = dragAreaY;
            dragRect.right = dragAreaX + dragAreaWidth;
            dragRect.bottom = dragAreaY + dragAreaHeight;
            
            // 检查鼠标是否在拖拽区域内
            if (PtInRect(&dragRect, pt)) {
                // 在拖拽区域内，允许拖拽
                return HTCAPTION;
            } else {
                // 在拖拽区域外，返回 HTTRANSPARENT，让鼠标事件传递给下面的窗口
                return HTTRANSPARENT;
            }
        }
        
    case WM_TRAYICON:
        // 处理托盘消息
        g_trayIcon.HandleMessage(wParam, lParam);
        return 0;
        
    case WM_COMMAND:
        // 处理托盘菜单命令
        g_trayIcon.HandleMessage(LOWORD(wParam), MAKELPARAM(WM_COMMAND, 0));
        return 0;
        
    case WM_DISPLAYCHANGE:
        // 显示设置改变时更新
        g_sysinfo.UpdateDisplayInfo();
        return 0;
        

        
    case WM_DROPFILES:
    {
#if USE_FILE_TRANSFER
        // 处理文件拖放
        HDROP hDrop = (HDROP)wParam;
        UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);

        for (UINT i = 0; i < fileCount; i++) {
            wchar_t filePath[MAX_PATH];
            if (DragQueryFile(hDrop, i, filePath, MAX_PATH)) {
                // 将文件添加到中转站
                g_transferstation.AddFile(filePath);
                LOG_INFO("File dropped: %ls", filePath);
            }
        }

        DragFinish(hDrop);
#endif
        return 0;
    }
        
    case WM_HOTKEY:
        // 处理全局热键
        if (wParam == 1) {
            // Ctrl+Shift+Z 退出
            ::PostQuitMessage(0);
            g_running = false;
            return 0;
        }
        break;
        
    case WM_KEYDOWN:
        // 处理快捷键
        if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
            // Ctrl+Shift+Z 退出
            ::PostQuitMessage(0);
            g_running = false;
            return 0;
        }
        break;
        
    case WM_LBUTTONDOWN:
    {
        // 检查是否在文件列表项上按下鼠标
        if (g_islandExpanded) {
            // 这里需要检测鼠标是否在文件列表项上
            // 由于我们使用 ImGui 绘制界面，需要在 ImGui 绘制循环中处理鼠标事件
            // 这里我们只是设置一个标记，实际的拖放操作在 ImGui 绘制循环中处理
        }
        return 0;
    }
    
    case WM_DESTROY:
        ::PostQuitMessage(0);
        g_running = false;
        return 0;
    }
    
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

// 创建窗口
HWND CreateMainWindow() {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, 
                      GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, 
                      L"DynamicIsland", nullptr };
    if (!::RegisterClassEx(&wc)) {
        return nullptr;
    }

    // 创建透明窗口
    HWND hwnd = ::CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"DynamicIsland",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hwnd) return nullptr;

#if USE_FILE_TRANSFER
    // 注册窗口为拖放目标
    ::DragAcceptFiles(hwnd, TRUE);
#endif

    return hwnd;
}

// 初始化应用程序
bool InitializeApp(bool silentStart) {
    // 清除旧日志
    FILE* f = fopen("dynamicisland.log", "w");
    if(f) fclose(f);
    
    LOG_INFO("=== DynamicIsland Starting ===");
    LOG_INFO("silentStart=%d", silentStart);
    
    // 初始化 COM 库
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to initialize COM: 0x%X", hr);
        return false;
    }
    
    // 1. 加载配置
    LOG_INFO("Loading config...");
    if (!g_config.Load()) {
        LOG_INFO("Config not found, creating default");
        g_config.Save();
    }
    LOG_INFO("Config loaded: opacity=%.2f", g_config.GetAppearance().opacity);
    
    // 2. 初始化系统信息监控
    LOG_INFO("Initializing sysinfo...");
    if (!g_sysinfo.Initialize()) {
        LOG_ERROR("Failed to initialize sysinfo");
        MessageBox(nullptr, L"Failed to initialize system info monitor", L"Error", MB_OK);
        return false;
    }
    LOG_INFO("Sysinfo initialized");
    
    // 3. 启动监控线程
    LOG_INFO("Starting monitoring thread...");
    g_sysinfo.StartMonitoring();
    LOG_INFO("Monitoring started");
    
    // 4. 初始化任务计划程序 (检查是否需要注册开机启动)
    LOG_INFO("Initializing scheduler...");
    g_scheduler.Initialize();  // 初始化COM
    if (g_config.GetBehavior().start_with_windows) {
        LOG_INFO("Startup enabled, checking registration...");
        if (!g_scheduler.IsRegistered()) {
            LOG_INFO("Not registered, registering...");
            TaskConfig taskConfig;
            taskConfig.delayStart = true;
            taskConfig.delaySeconds = 30;
            taskConfig.hidden = true;
            g_scheduler.Register(taskConfig);
        }
    }
    
    // 5. 创建窗口
    LOG_INFO("Creating main window...");
    g_hwnd = CreateMainWindow();
    LOG_INFO("Window handle: %p", g_hwnd);
    if (!g_hwnd) {
        MessageBox(nullptr, L"Failed to create window", L"Error", MB_OK);
        return false;
    }
    
    // 6. 初始化D3D
    LOG_INFO("Initializing D3D...");
    if (!CreateDeviceD3D(g_hwnd)) {
        LOG_ERROR("Failed to initialize D3D");
        CleanupDeviceD3D();
        ::DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
        return false;
    }
    LOG_INFO("D3D initialized: device=%p", g_pd3dDevice);
    
    // 7. 初始化托盘图标
    LOG_INFO("Initializing tray icon...");
    if (!g_trayIcon.Initialize(g_hwnd, WM_TRAYICON)) {
        LOG_ERROR("Failed to initialize tray icon");
        // 托盘初始化失败不是致命错误
    } else {
        LOG_INFO("Tray icon initialized");
    }
    
    // 8. 设置托盘回调
    g_trayIcon.SetShowHideCallback([]() {
        g_islandVisible = !g_islandVisible;
        // 窗口始终保持显示（透明），只控制灵动岛的绘制
    });
    
    g_trayIcon.SetExpandCallback([]() {
        g_islandExpanded = !g_islandExpanded;
        // 更新托盘菜单状态
        g_trayIcon.UpdateMenuState(
            g_windowVisible,
            g_islandExpanded,
            PerformanceMode::BALANCED, // 性能模式暂时使用默认值
            IslandPosition::TOP_CENTER, // 位置暂时使用默认值
            g_config.GetBehavior().start_with_windows
        );
    });
    
    g_trayIcon.SetPerformanceCallback([](PerformanceMode mode) {
        // 性能模式设置
    });
    
    g_trayIcon.SetPositionCallback([](IslandPosition pos) {
        g_config.GetIsland().position = 
            (pos == IslandPosition::TOP_CENTER) ? "top-center" :
            (pos == IslandPosition::TOP_LEFT) ? "top-left" : "follow-taskbar";
        g_config.Save();
    });
    
    g_trayIcon.SetStartupCallback([](bool enabled) {
        g_config.GetBehavior().start_with_windows = enabled;
        g_config.Save();
        
        if (enabled) {
            TaskConfig taskConfig;
            taskConfig.delayStart = true;
            taskConfig.delaySeconds = 30;
            taskConfig.hidden = true;
            g_scheduler.Register(taskConfig);
        } else {
            g_scheduler.Unregister();
        }
    });
    
    g_trayIcon.SetExitCallback([]() {
        g_running = false;
        PostQuitMessage(0);
    });

    g_trayIcon.SetSettingsCallback([]() {
        g_showSettings = true;
    });

    // 9. 初始化ImGui
    LOG_INFO("Initializing ImGui...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    LOG_INFO("ImGui context created");
    
    // 设置ImGui样式
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 20.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.TabRounding = 8.0f;
    
    // 应用配置中的外观设置
    auto& appearance = g_config.GetAppearance();
    style.Alpha = appearance.opacity;
    
    // 初始化平台/渲染器后端
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    
    // 11. 显示窗口
    if (!silentStart && !g_config.GetBehavior().start_minimized) {
        ::ShowWindow(g_hwnd, SW_SHOW);
        g_windowVisible = true;
    } else {
        // 静默启动时可以选择不显示窗口
        if (g_config.GetBehavior().start_minimized) {
            ::ShowWindow(g_hwnd, SW_HIDE);
            g_windowVisible = false;
        } else {
            ::ShowWindow(g_hwnd, SW_SHOW);
            g_windowVisible = true;
        }
    }
    
    // 12. 注册全局热键 Ctrl+Shift+Z
    if (!RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_SHIFT, 'Z')) {
        LOG_ERROR("Failed to register hotkey");
    } else {
        LOG_INFO("Hotkey Ctrl+Shift+Z registered");
    }
    
    // 13. 更新托盘菜单状态
    g_trayIcon.UpdateMenuState(
        g_windowVisible,
        g_islandExpanded, // 传递展开状态
        PerformanceMode::BALANCED, // 默认性能模式
        IslandPosition::TOP_CENTER, // 默认位置
        g_config.GetBehavior().start_with_windows
    );
    
    return true;
}



// 清理应用程序
void ShutdownApp() {
    // 保存配置
    g_config.Save();
    
    // 清理ImGui
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    // 清理托盘
    g_trayIcon.Shutdown();
    
    // 清理系统信息监控
    g_sysinfo.Shutdown();
    
    // 清理任务计划程序
    g_scheduler.Shutdown();
    
    // 清理D3D
    CleanupDeviceD3D();
    
    // 销毁窗口
    if (g_hwnd) {
        ::DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    
    // 注销全局热键
    UnregisterHotKey(g_hwnd, 1);
    LOG_INFO("Hotkey unregistered");
    
    // 注销窗口类
    ::UnregisterClass(L"DynamicIsland", GetModuleHandle(nullptr));
    
    // 清理 COM 库
    CoUninitialize();
    LOG_INFO("COM uninitialized");
}

// 主函数
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPWSTR lpCmdLine, int nCmdShow) {
    // 最早期的日志
    FILE* earlyLog = fopen("dynamicisland_early.log", "w");
    if (earlyLog) {
        fprintf(earlyLog, "WinMain started\n");
        fclose(earlyLog);
    }
    
    // 解析命令行参数
    bool silentStart = false;
    for (int i = 1; i < __argc; ++i) {
        char arg[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, __wargv[i], -1, arg, 256, nullptr, nullptr);
        if (_stricmp(arg, "/background") == 0) {
            silentStart = true;
        }
    }
    
    earlyLog = fopen("dynamicisland_early.log", "a");
    if (earlyLog) {
        fprintf(earlyLog, "silentStart=%d\n", silentStart);
        fclose(earlyLog);
    }
    
    // 单实例保护
    HANDLE hMutex = CreateMutex(nullptr, FALSE, TEXT("Global\\DynamicIsland"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        earlyLog = fopen("dynamicisland_early.log", "a");
        if (earlyLog) {
            fprintf(earlyLog, "Another instance running, exiting\n");
            fclose(earlyLog);
        }
        // 已有实例运行，激活它
        HWND existingWnd = FindWindow(L"DynamicIsland", nullptr);
        if (existingWnd) {
            ShowWindow(existingWnd, SW_SHOW);
            SetForegroundWindow(existingWnd);
        }
        return 0;
    }
    
    earlyLog = fopen("dynamicisland_early.log", "a");
    if (earlyLog) {
        fprintf(earlyLog, "Mutex created, calling InitializeApp\n");
        fclose(earlyLog);
    }
    
    // 初始化应用程序
    if (!InitializeApp(silentStart)) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }
    
    // 主循环
    LOG_INFO("Entering main loop");
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    
    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    
    // 动画变量
    float animationY = 20.0f; // 初始位置
    float targetY = 20.0f;     // 目标位置
    
    while (g_running) {
        // 处理Windows消息
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }
        
        if (!g_running)
            break;
        
        // 计算delta time
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 动画逻辑 - 使用缓动函数
        const float animationSpeed = 8.0f; // 动画速度
        animationY += (targetY - animationY) * deltaTime * animationSpeed;
        
        // 开始ImGui帧
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // 检测是否在桌面状态
        bool isDesktop = false;
        HWND foregroundWindow = GetForegroundWindow();
        
        // 检查是否是桌面或资源管理器
        HWND desktopWindow = GetDesktopWindow();
        HWND shellWindow = GetShellWindow();
        if (!foregroundWindow || foregroundWindow == desktopWindow || foregroundWindow == shellWindow) {
            isDesktop = true;
        } else {
            // 检查前景窗口是否是资源管理器
            wchar_t className[256];
            GetClassNameW(foregroundWindow, className, sizeof(className) / sizeof(wchar_t));
            if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0) {
                isDesktop = true;
            }
        }
        
        // 检测是否有全屏或最大化窗口
        bool isFullscreen = false;
        if (!isDesktop && foregroundWindow) {
            // 检查是否最大化
            WINDOWPLACEMENT placement;
            if (GetWindowPlacement(foregroundWindow, &placement)) {
                if (placement.showCmd == SW_SHOWMAXIMIZED) {
                    isFullscreen = true;
                }
            }
            
            // 检查是否全屏
            if (!isFullscreen) {
                RECT rect;
                if (GetWindowRect(foregroundWindow, &rect)) {
                    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                    if (rect.right - rect.left >= screenWidth - 10 && rect.bottom - rect.top >= screenHeight - 10) {
                        isFullscreen = true;
                    }
                }
            }
        }
        
        // 桌面状态时不隐藏
        if (isDesktop) {
            isFullscreen = false;
        }
        
        // 只有当灵动岛可见时才绘制
        if (g_islandVisible) {
            // 灵动岛配置 - 增加展开状态的窗口大小
            ImVec2 size = g_islandExpanded ? ImVec2(600.0f, 300.0f) : ImVec2(400.0f, 80.0f);
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            ImVec2 pos;
            pos.x = (screenWidth - size.x) * 0.5f;
            
            // 检测鼠标位置
            POINT mousePos;
            GetCursorPos(&mousePos);
            bool isMouseOver = false;
            
            if (isFullscreen) {
                // 全屏时收起，只露出10像素
                pos.y = -size.y + 10;
                // 检查鼠标是否悬停在露出的部分
                RECT islandRect;
                islandRect.left = (int)pos.x;
                islandRect.top = (int)pos.y;
                islandRect.right = (int)(pos.x + size.x);
                islandRect.bottom = (int)(pos.y + size.y);
                isMouseOver = PtInRect(&islandRect, mousePos);
            }
            
            // 设置目标位置
            if (isMouseOver) {
                targetY = 20.0f; // 展开位置
            } else if (!isFullscreen) {
                targetY = 20.0f; // 正常显示位置
            } else {
                targetY = -size.y + 10; // 收起位置
            }
            
            // 当鼠标悬停时，即使是全屏状态也显示
            if (isMouseOver) {
                isFullscreen = false; // 临时禁用全屏检测
            }
            
            // 使用动画位置
            pos.y = animationY;

            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(size);

            // 设置窗口区域，只在 Imgui 显示区域捕获鼠标
            POINT topLeft = { (LONG)pos.x, (LONG)pos.y };
            ScreenToClient(g_hwnd, &topLeft);
            HRGN imguiRgn = CreateRectRgn(topLeft.x, topLeft.y, topLeft.x + (LONG)size.x, topLeft.y + (LONG)size.y);
            SetWindowRgn(g_hwnd, imguiRgn, TRUE);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, size.y * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            
            // 透明背景
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
            
            bool open = true;
            if (ImGui::Begin("DynamicIsland", &open,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoBackground)) {
                
                // 获取外观配置
                auto& appearance = g_config.GetAppearance();
                
                // 绘制胶囊背景
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p0 = ImGui::GetWindowPos();
                ImU32 bgColor;
                if (appearance.style == "white") {
                    bgColor = IM_COL32(240, 240, 240, 200); // 白色透明
                } else {
                    bgColor = IM_COL32(30, 30, 40, 200); // 深色毛玻璃
                }
                dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), bgColor, size.y * 0.5f);
                
                // 状态指示点
                float cpuUsage = g_sysinfo.GetCpuUsage();
                ImU32 dotColor;
                if (cpuUsage < 50.0f) dotColor = IM_COL32(0, 255, 0, 255);
                else if (cpuUsage < 80.0f) dotColor = IM_COL32(255, 255, 0, 255);
                else dotColor = IM_COL32(255, 0, 0, 255);
                
                float cy = p0.y + size.y * 0.5f;
                dl->AddCircleFilled(ImVec2(p0.x + 20, cy), 6.0f, dotColor);
                
                // 时间 - 液晶电子数码表样式
                char timeBuf[16];
                SYSTEMTIME st;
                GetLocalTime(&st);
                sprintf(timeBuf, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
                
                // 液晶电子数码表样式
                ImGui::SetCursorPos(ImVec2(40, 15));
                // 根据背景颜色设置文本颜色
                if (appearance.style == "white") {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 200, 100, 255)); // 深绿色
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 100, 255)); // 绿色
                }
                // 保存当前字体大小
                float oldFontSize = ImGui::GetFont()->Scale;
                // 设置较大的字体大小
                ImGui::GetFont()->Scale = 1.2f;
                ImGui::TextUnformatted(timeBuf);
                // 恢复原始字体大小
                ImGui::GetFont()->Scale = oldFontSize;
                ImGui::PopStyleColor();
                
                // 根据背景颜色设置文本颜色
                if (appearance.style == "white") {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(30, 30, 40, 255)); // 深色文本
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 240, 240, 255)); // 浅色文本
                }
                
                if (!g_islandExpanded) {
                    // 收起状态 - 显示基本信息
                    // 系统信息
                    ImGui::SetCursorPos(ImVec2(40, 45));
                    float cpu = g_sysinfo.GetCpuUsage();
                    auto memInfo = g_sysinfo.GetMemoryInfo();
                    float memUsedGB = memInfo.used_bytes / (1024.0f * 1024.0f * 1024.0f);
                    float memTotalGB = memInfo.total_bytes / (1024.0f * 1024.0f * 1024.0f);
                    ImGui::Text("CPU: %.1f%% | Mem: %.1f/%.1f GB", cpu, memUsedGB, memTotalGB);
                    
                    // 电量
                    ImGui::SetCursorPos(ImVec2(280, 30));
                    float battery = g_sysinfo.GetBatteryPercent();
                    ImGui::Text("Batt: %.0f%%", battery);
                    
                    // 充电状态
                    auto batteryInfo = g_sysinfo.GetBatteryInfo();
                    if (batteryInfo.is_plugged) {
                        ImGui::SetCursorPos(ImVec2(280, 45));
                        ImGui::Text("Charging");
                    }
                    
                    // 中转站状态
#if USE_FILE_TRANSFER
                    size_t fileCount = g_transferstation.GetFileCount();
                    if (fileCount > 0) {
                        uint64_t totalSize = g_transferstation.GetTotalSize();
                        // 格式化文件大小
                        std::string sizeStr;
                        if (totalSize < 1024) {
                            sizeStr = std::to_string(totalSize) + " B";
                        } else if (totalSize < 1024 * 1024) {
                            sizeStr = std::to_string((int)(totalSize / 1024)) + " KB";
                        } else if (totalSize < 1024 * 1024 * 1024) {
                            sizeStr = std::to_string((float)totalSize / (1024 * 1024)) + " MB";
                        } else {
                            sizeStr = std::to_string((float)totalSize / (1024 * 1024 * 1024)) + " GB";
                        }
                        ImGui::SetCursorPos(ImVec2(280, 60));
                        ImGui::Text("%zu files · %s", fileCount, sizeStr.c_str());
                    }
#endif
                } else {
                    // 展开状态 - 显示更多信息
                    // 基本系统信息
                    ImGui::SetCursorPos(ImVec2(40, 45));
                    float cpu = g_sysinfo.GetCpuUsage();
                    auto memInfo = g_sysinfo.GetMemoryInfo();
                    float memUsedGB = memInfo.used_bytes / (1024.0f * 1024.0f * 1024.0f);
                    float memTotalGB = memInfo.total_bytes / (1024.0f * 1024.0f * 1024.0f);
                    ImGui::Text("CPU: %.1f%% | Mem: %.1f/%.1f GB", cpu, memUsedGB, memTotalGB);
                    
                    // GPU信息
                    auto gpuInfo = g_sysinfo.GetGPUInfo();
                    if (gpuInfo.available) {
                        ImGui::SetCursorPos(ImVec2(40, 65));
                        ImGui::Text("GPU: %.1f%% | %s", gpuInfo.usage_percent, gpuInfo.name.c_str());
                    }
                    
                    // 网络速度
                    auto networkInfo = g_sysinfo.GetNetworkInfo();
                    if (networkInfo.is_connected) {
                        ImGui::SetCursorPos(ImVec2(40, 85));
                        ImGui::Text("↓%.1f Mbps | ↑%.1f Mbps", networkInfo.download_speed_mbps, networkInfo.upload_speed_mbps);
                    }
                    
                    // 电量
                    ImGui::SetCursorPos(ImVec2(300, 45));
                    float battery = g_sysinfo.GetBatteryPercent();
                    ImGui::Text("Battery: %.0f%%", battery);
                    
                    // 充电状态
                    auto batteryInfo = g_sysinfo.GetBatteryInfo();
                    if (batteryInfo.is_plugged) {
                        ImGui::SetCursorPos(ImVec2(300, 65));
                        ImGui::Text("Charging");
                    } else if (batteryInfo.remaining_minutes > 0) {
                        ImGui::SetCursorPos(ImVec2(300, 65));
                        ImGui::Text("Remaining: %d min", batteryInfo.remaining_minutes);
                    }
                    
                    // 显示信息
                    auto displayInfo = g_sysinfo.GetDisplayInfo();
                    ImGui::SetCursorPos(ImVec2(300, 85));
                    ImGui::Text("Display: %dx%d @ %dHz", displayInfo.resolution_x, displayInfo.resolution_y, displayInfo.refresh_rate_hz);
                    
                    // 文件中转站界面
#if USE_FILE_TRANSFER
                    ImGui::SetCursorPos(ImVec2(40, 110));
                    ImGui::Text("File Transfer Station:");

                    auto files = g_transferstation.GetFiles();
                    if (files.empty()) {
                        ImGui::SetCursorPos(ImVec2(60, 130));
                        ImGui::Text("Drop files here to add to transfer station");
                    } else {
                        // 显示统计信息
                        size_t fileCount = g_transferstation.GetFileCount();
                        uint64_t totalSize = g_transferstation.GetTotalSize();
                        // 格式化文件大小
                        std::string sizeStr;
                        if (totalSize < 1024) {
                            sizeStr = std::to_string(totalSize) + " B";
                        } else if (totalSize < 1024 * 1024) {
                            sizeStr = std::to_string((int)(totalSize / 1024)) + " KB";
                        } else if (totalSize < 1024 * 1024 * 1024) {
                            sizeStr = std::to_string((float)totalSize / (1024 * 1024)) + " MB";
                        } else {
                            sizeStr = std::to_string((float)totalSize / (1024 * 1024 * 1024)) + " GB";
                        }
                        ImGui::SetCursorPos(ImVec2(60, 130));
                        ImGui::Text("Total: %zu files · %s", fileCount, sizeStr.c_str());
                        
                        // 显示最近添加的文件
                        ImGui::SetCursorPos(ImVec2(60, 150));
                        ImGui::Text("Recent files:");
                        
                        // 排序方式
                        static int sortMode = 0; // 0: 时间降序, 1: 名称升序, 2: 大小降序
                        
                        // 排序选项
                        ImGui::SetCursorPos(ImVec2(60, 150));
                        ImGui::Text("Sort by:");
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Time", sortMode == 0)) sortMode = 0;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Name", sortMode == 1)) sortMode = 1;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Size", sortMode == 2)) sortMode = 2;
                        
                        // 排序文件
                        std::vector<FileInfo> sortedFiles = files;
                        switch (sortMode) {
                        case 0: // 时间降序
                            std::sort(sortedFiles.begin(), sortedFiles.end(), [](const FileInfo& a, const FileInfo& b) {
                                return a.added_time > b.added_time;
                            });
                            break;
                        case 1: // 名称升序
                            std::sort(sortedFiles.begin(), sortedFiles.end(), [](const FileInfo& a, const FileInfo& b) {
                                return a.name < b.name;
                            });
                            break;
                        case 2: // 大小降序
                            std::sort(sortedFiles.begin(), sortedFiles.end(), [](const FileInfo& a, const FileInfo& b) {
                                return a.size > b.size;
                            });
                            break;
                        }
                        
                        // 显示所有文件
                        size_t displayCount = sortedFiles.size();
                        for (size_t i = 0; i < displayCount; i++) {
                            const auto& file = sortedFiles[i];
                            
                            // 格式化文件大小
                            std::string fileSizeStr;
                            if (file.size < 1024) {
                                fileSizeStr = std::to_string(file.size) + " B";
                            } else if (file.size < 1024 * 1024) {
                                fileSizeStr = std::to_string((int)(file.size / 1024)) + " KB";
                            } else if (file.size < 1024 * 1024 * 1024) {
                                fileSizeStr = std::to_string((float)file.size / (1024 * 1024)) + " MB";
                            } else {
                                fileSizeStr = std::to_string((float)file.size / (1024 * 1024 * 1024)) + " GB";
                            }
                            
                            // 格式化添加时间
                            std::time_t time = std::chrono::system_clock::to_time_t(file.added_time);
                            std::tm localTime;
                            localtime_s(&localTime, &time);
                            char timeStr[20];
                            sprintf(timeStr, "%04d-%02d-%02d %02d:%02d",
                                    localTime.tm_year + 1900, localTime.tm_mon + 1,
                                    localTime.tm_mday, localTime.tm_hour, localTime.tm_min);
                            
                            // 获取文件扩展名
                            std::wstring ext = L"";
                            size_t dotPos = file.name.find_last_of(L'.');
                            if (dotPos != std::wstring::npos) {
                                ext = file.name.substr(dotPos + 1);
                            }
                            
                            // 为每个文件项创建一个可拖动的区域
                            std::string fileItemId = "fileItem##" + std::to_string(i);
                            ImGui::PushID(fileItemId.c_str());
                            
                            // 开始一个可拖动的项目
                            bool isItemHovered = ImGui::IsItemHovered();
                            bool isMouseDown = ImGui::IsMouseDown(0);
                            bool isMouseDragging = ImGui::IsMouseDragging(0);
                            
                            // 计算行高
                            float lineHeight = 35.0f;
                            float yPos = 180 + i * lineHeight;
                            
                            // 显示文件类型图标
                            ImGui::SetCursorPos(ImVec2(70, yPos));
                            // 简单的文件类型图标
                            std::string iconText = "📄";
                            if (ext == L"txt" || ext == L"text") iconText = "📝";
                            else if (ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"gif" || ext == L"bmp") iconText = "🖼️";
                            else if (ext == L"mp3" || ext == L"wav" || ext == L"flac" || ext == L"m4a") iconText = "🎵";
                            else if (ext == L"mp4" || ext == L"avi" || ext == L"mov" || ext == L"wmv") iconText = "🎬";
                            else if (ext == L"doc" || ext == L"docx") iconText = "📃";
                            else if (ext == L"xls" || ext == L"xlsx") iconText = "📊";
                            else if (ext == L"pdf") iconText = "📄";
                            else if (ext == L"zip" || ext == L"rar" || ext == L"7z" || ext == L"tar" || ext == L"gz") iconText = "📦";
                            else if (ext == L"exe" || ext == L"msi") iconText = "💾";
                            else if (ext == L"dll" || ext == L"lib" || ext == L"so") iconText = "🔧";
                            else if (ext == L"cpp" || ext == L"h" || ext == L"hpp" || ext == L"c" || ext == L"cs" || ext == L"js" || ext == L"ts" || ext == L"py" || ext == L"java") iconText = "📄";
                            ImGui::Text(iconText.c_str());
                            
                            // 显示文件信息
                            ImGui::SetCursorPos(ImVec2(100, yPos));
                            ImGui::Text("%s", file.name.c_str());
                            
                            // 显示文件大小
                            ImGui::SetCursorPos(ImVec2(300, yPos));
                            ImGui::Text("%s", fileSizeStr.c_str());
                            
                            // 显示添加时间
                            ImGui::SetCursorPos(ImVec2(380, yPos));
                            ImGui::Text("%s", timeStr);
                            
                            // 检查是否在文件项上按下鼠标并拖动
                            if (isItemHovered && isMouseDown && isMouseDragging) {
                                // 启动拖放操作
                                std::vector<std::wstring> filePaths;
                                filePaths.push_back(file.path);
                                
                                IDataObject* pDataObject = nullptr;
                                HRESULT hr = CreateFileDropDataObject(filePaths, &pDataObject);
                                if (SUCCEEDED(hr)) {
                                    IDropSource* pDropSource = new CDropSource();
                                    if (pDropSource) {
                                        DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
                                        DWORD dwResult = 0;
                                        hr = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwResult);
                                        
                                        pDropSource->Release();
                                    }
                                    pDataObject->Release();
                                }
                            }
                            
                            // 操作按钮 - 调整位置
                            float buttonStartX = 480.0f;
                            float buttonWidth = 50.0f;
                            float buttonSpacing = 5.0f;
                            
                            // 打开按钮
                            ImGui::SetCursorPos(ImVec2(buttonStartX, yPos - 2));
                            std::string openButton = "Open##file" + std::to_string(i);
                            if (ImGui::Button(openButton.c_str(), ImVec2(buttonWidth, 20))) {
                                // 找到文件在原始列表中的索引
                                size_t originalIndex = 0;
                                for (; originalIndex < files.size(); originalIndex++) {
                                    if (files[originalIndex].path == file.path) {
                                        break;
                                    }
                                }
                                if (originalIndex < files.size()) {
                                    g_transferstation.OpenFile(originalIndex);
                                }
                            }
                            
                            // 复制按钮
                            ImGui::SetCursorPos(ImVec2(buttonStartX + buttonWidth + buttonSpacing, yPos - 2));
                            std::string copyButton = "Copy##file" + std::to_string(i);
                            if (ImGui::Button(copyButton.c_str(), ImVec2(buttonWidth, 20))) {
                                // 找到文件在原始列表中的索引
                                size_t originalIndex = 0;
                                for (; originalIndex < files.size(); originalIndex++) {
                                    if (files[originalIndex].path == file.path) {
                                        break;
                                    }
                                }
                                if (originalIndex < files.size()) {
                                    // 打开文件夹选择对话框
                                    BROWSEINFO bi = { 0 };
                                    bi.lpszTitle = L"Select destination folder";
                                    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
                                    if (pidl) {
                                        wchar_t path[MAX_PATH];
                                        if (SHGetPathFromIDList(pidl, path)) {
                                            std::wstring destPath(path);
                                            bool success = g_transferstation.TransferCopyFile(originalIndex, destPath);
                                            if (success) {
                                                // 显示成功提示
                                                MessageBox(nullptr, L"File copied successfully", L"Success", MB_OK);
                                            } else {
                                                // 显示错误提示
                                                MessageBox(nullptr, L"Failed to copy file", L"Error", MB_OK);
                                            }
                                        }
                                        CoTaskMemFree(pidl);
                                    }
                                }
                            }
                            
                            // 移动按钮
                            ImGui::SetCursorPos(ImVec2(buttonStartX + (buttonWidth + buttonSpacing) * 2, yPos - 2));
                            std::string moveButton = "Move##file" + std::to_string(i);
                            if (ImGui::Button(moveButton.c_str(), ImVec2(buttonWidth, 20))) {
                                // 找到文件在原始列表中的索引
                                size_t originalIndex = 0;
                                for (; originalIndex < files.size(); originalIndex++) {
                                    if (files[originalIndex].path == file.path) {
                                        break;
                                    }
                                }
                                if (originalIndex < files.size()) {
                                    // 打开文件夹选择对话框
                                    BROWSEINFO bi = { 0 };
                                    bi.lpszTitle = L"Select destination folder";
                                    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
                                    if (pidl) {
                                        wchar_t path[MAX_PATH];
                                        if (SHGetPathFromIDList(pidl, path)) {
                                            std::wstring destPath(path);
                                            bool success = g_transferstation.TransferMoveFile(originalIndex, destPath);
                                            if (success) {
                                                // 显示成功提示
                                                MessageBox(nullptr, L"File moved successfully", L"Success", MB_OK);
                                            } else {
                                                // 显示错误提示
                                                MessageBox(nullptr, L"Failed to move file", L"Error", MB_OK);
                                            }
                                        }
                                        CoTaskMemFree(pidl);
                                    }
                                }
                            }
                            
                            // 预览按钮
                            ImGui::SetCursorPos(ImVec2(buttonStartX + (buttonWidth + buttonSpacing) * 3, yPos - 2));
                            std::string previewButton = "Preview##file" + std::to_string(i);
                            if (ImGui::Button(previewButton.c_str(), ImVec2(buttonWidth, 20))) {
                                // 找到文件在原始列表中的索引
                                size_t originalIndex = 0;
                                for (; originalIndex < files.size(); originalIndex++) {
                                    if (files[originalIndex].path == file.path) {
                                        break;
                                    }
                                }
                                if (originalIndex < files.size()) {
                                    g_selectedFileIndex = originalIndex;
                                    g_showPreview = true;
                                }
                            }
                            
                            // 删除按钮
                            ImGui::SetCursorPos(ImVec2(buttonStartX + (buttonWidth + buttonSpacing) * 4, yPos - 2));
                            std::string deleteButton = "Delete##file" + std::to_string(i);
                            if (ImGui::Button(deleteButton.c_str(), ImVec2(buttonWidth, 20))) {
                                // 找到文件在原始列表中的索引
                                size_t originalIndex = 0;
                                for (; originalIndex < files.size(); originalIndex++) {
                                    if (files[originalIndex].path == file.path) {
                                        break;
                                    }
                                }
                                if (originalIndex < files.size()) {
                                    g_transferstation.TransferDeleteFile(originalIndex);
                                }
                            }
                            
                            ImGui::PopID();
                        }
                    }
#endif
                }

                // 恢复文本颜色
                ImGui::PopStyleColor();
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        // 文件预览窗口
#if USE_FILE_TRANSFER
        if (g_showPreview && g_selectedFileIndex != -1) {
            auto files = g_transferstation.GetFiles();
            if (g_selectedFileIndex < files.size()) {
                const auto& file = files[g_selectedFileIndex];
                auto previewInfo = g_transferstation.GetFilePreviewInfo(g_selectedFileIndex);
                
                // 设置预览窗口位置和大小
                ImVec2 previewSize(800.0f, 600.0f);
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                ImVec2 previewPos((screenWidth - previewSize.x) * 0.5f, (screenHeight - previewSize.y) * 0.5f);
                
                ImGui::SetNextWindowPos(previewPos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(previewSize, ImGuiCond_Always);
                
                // 透明背景
                ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(30, 30, 40, 240));
                
                bool previewOpen = true;
                if (ImGui::Begin("File Preview", &previewOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
                    // 显示文件名
                    ImGui::Text("File: %s", file.name.c_str());
                    ImGui::Separator();
                    
                    // 显示文件详细信息
                    ImGui::Text("File Type: %s", previewInfo.file_type.c_str());
                    ImGui::Text("Extension: %s", previewInfo.file_extension.c_str());
                    
                    // 格式化文件大小
                    std::string sizeStr;
                    if (file.size < 1024) {
                        sizeStr = std::to_string(file.size) + " B";
                    } else if (file.size < 1024 * 1024) {
                        sizeStr = std::to_string((int)(file.size / 1024)) + " KB";
                    } else if (file.size < 1024 * 1024 * 1024) {
                        sizeStr = std::to_string((float)file.size / (1024 * 1024)) + " MB";
                    } else {
                        sizeStr = std::to_string((float)file.size / (1024 * 1024 * 1024)) + " GB";
                    }
                    ImGui::Text("Size: %s", sizeStr.c_str());
                    
                    ImGui::Text("Creation Time: %s", previewInfo.creation_time.c_str());
                    ImGui::Text("Last Modified: %s", previewInfo.last_modified_time.c_str());
                    ImGui::Text("Last Accessed: %s", previewInfo.last_access_time.c_str());
                    ImGui::Text("Path: %s", file.path.c_str());
                    
                    ImGui::Separator();
                    
                    // 显示文件内容预览
                    if (previewInfo.is_text) {
                        ImGui::Text("Text Content:");
                        ImGui::BeginChild("TextContent", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
                        ImGui::TextUnformatted(previewInfo.text_content.c_str());
                        ImGui::EndChild();
                    } else if (previewInfo.is_image) {
                        ImGui::Text("Image Preview:");
                        ImGui::BeginChild("ImageContent", ImVec2(0, 300), true);
                        // 这里可以添加图片预览代码，目前显示占位符
                        ImGui::Text("[Image Preview - Implement with DirectX or ImGui image loading]");
                        ImGui::EndChild();
                    } else {
                        ImGui::Text("No preview available for this file type");
                    }
                    
                    // 关闭按钮
                    if (!previewOpen) {
                        g_showPreview = false;
                        g_selectedFileIndex = -1;
                    }
                }

                ImGui::End();
                ImGui::PopStyleColor();
            }
        }
#endif

        // 设置窗口
        if (g_showSettings) {
            ImVec2 settingsSize(600.0f, 400.0f);
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            ImVec2 settingsPos((screenWidth - settingsSize.x) * 0.5f, (screenHeight - settingsSize.y) * 0.5f);

            ImGui::SetNextWindowPos(settingsPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(settingsSize, ImGuiCond_Always);

            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(40, 40, 50, 255));

            bool settingsOpen = true;
            if (ImGui::Begin("设置", &settingsOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("DynamicIsland 设置");
                ImGui::Separator();

                // 设置分类导航
                static int selectedCategory = 0;
                ImGui::BeginChild("Categories", ImVec2(150, 0), true);
                if (ImGui::Selectable("通用", selectedCategory == 0)) selectedCategory = 0;
                if (ImGui::Selectable("外观", selectedCategory == 1)) selectedCategory = 1;
                if (ImGui::Selectable("通知", selectedCategory == 2)) selectedCategory = 2;
                if (ImGui::Selectable("文件中转站", selectedCategory == 3)) selectedCategory = 3;
                if (ImGui::Selectable("高级", selectedCategory == 4)) selectedCategory = 4;
                if (ImGui::Selectable("关于", selectedCategory == 5)) selectedCategory = 5;
                ImGui::EndChild();

                ImGui::SameLine();

                // 设置内容区域
                ImGui::BeginChild("Content", ImVec2(0, 0), true);
                switch (selectedCategory) {
                    case 0: // 通用
                        ImGui::Text("通用设置");
                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("开机启动")) {
                            ImGui::Text("设置程序是否在系统启动时自动运行");
                        }
                        if (ImGui::CollapsingHeader("刷新频率")) {
                            ImGui::Text("调整系统信息的刷新速度");
                        }
                        break;
                    case 1: // 外观
                        ImGui::Text("外观设置");
                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("主题")) {
                            ImGui::Text("选择灵动岛的外观主题");
                        }
                        if (ImGui::CollapsingHeader("动画")) {
                            ImGui::Text("配置展开/收起动画效果");
                        }
                        break;
                    case 2: // 通知
                        ImGui::Text("通知设置");
                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("系统通知")) {
                            ImGui::Text("配置通知提醒功能");
                        }
                        break;
                    case 3: // 文件中转站
                        ImGui::Text("文件中转站设置");
                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("存储设置")) {
                            ImGui::Text("配置文件中转站的存储位置和限制");
                        }
                        break;
                    case 4: // 高级
                        ImGui::Text("高级设置");
                        ImGui::Separator();
                        if (ImGui::CollapsingHeader("调试选项")) {
                            ImGui::Text("调试和诊断选项");
                        }
                        break;
                    case 5: // 关于
                        ImGui::Text("关于");
                        ImGui::Separator();
                        ImGui::Text("DynamicIsland v1.0");
                        ImGui::Text("一个模仿苹果灵动岛的 Windows 系统监控工具");
                        ImGui::Text("");
                        ImGui::Text("功能:");
                        ImGui::BulletText("实时系统监控 (CPU, 内存, GPU, 网络)");
                        ImGui::BulletText("文件中转站功能");
                        ImGui::BulletText("可定制的性能和外观设置");
                        break;
                }
                ImGui::EndChild();

                // 关闭按钮
                if (!settingsOpen) {
                    g_showSettings = false;
                }
            }

            ImGui::End();
            ImGui::PopStyleColor();
        }

        // 渲染
        ImGui::Render();
        
        // 检查是否有绘制数据
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->CmdListsCount > 0) {
            if (frameCount < 10) {
                LOG_DEBUG("Frame %d: CmdListsCount=%d, TotalVtxCount=%d", 
                    frameCount, drawData->CmdListsCount, drawData->TotalVtxCount);
            }
        } else if (frameCount < 10) {
            LOG_DEBUG("Frame %d: No draw data!", frameCount);
        }
        
        // 清除背景 (透明)
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        
        // 绘制ImGui
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        
        frameCount++;
        
        // 呈现
        g_pSwapChain->Present(1, 0);
        
        // 更新窗口几何
        UpdateWindowGeometry();
    }
    
    // 清理
    ShutdownApp();
    
    if (hMutex) CloseHandle(hMutex);
    
    return 0;
}
