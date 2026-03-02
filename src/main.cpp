#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <chrono>
#include <stdio.h>

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

// 项目模块
#include "config.h"
#include "sysinfo.h"

#include "trayicon.h"
#include "scheduler.h"

// 简单日志宏
#define LOG(msg, ...) { FILE* f = fopen("dynamicisland.log", "a"); if(f) { fprintf(f, "[%s:%d] " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); fclose(f); } }
#define LOG_INFO(msg, ...) LOG("[INFO] " msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) LOG("[ERROR] " msg, ##__VA_ARGS__)

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

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
    
    // 主窗口大小：足够大以容纳灵动岛
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    
    // 全屏覆盖，这样灵动岛可以在任何位置显示
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    
    // 应用毛玻璃效果
    BOOL useBackdrop = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &useBackdrop, sizeof(useBackdrop));
    
    // 扩展框架到客户端区域，创建透明效果
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
    
    // 创建支持透明的渲染目标视图
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
        // 允许拖拽窗口
        {
            LRESULT hit = ::DefWindowProc(hWnd, msg, wParam, lParam);
            if (hit == HTCLIENT)
                return HTCAPTION;
            return hit;
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
        
    case WM_LBUTTONDOWN:
        // 开始拖动窗口
        if (wParam == MK_LBUTTON) {
            // 释放捕获
            ReleaseCapture();
            // 发送拖动消息
            SendMessage(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        }
        return 0;
        
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
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        wc.lpszClassName,
        L"DynamicIsland",
        WS_POPUP,
        0, 0, 800, 600,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hwnd) return nullptr;

    // 设置分层窗口属性以支持透明度
    ::SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    return hwnd;
}

// 初始化应用程序
bool InitializeApp(bool silentStart) {
    // 清除旧日志
    FILE* f = fopen("dynamicisland.log", "w");
    if(f) fclose(f);
    
    LOG_INFO("=== DynamicIsland Starting ===");
    LOG_INFO("silentStart=%d", silentStart);
    
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
    
    // 12. 更新托盘菜单状态
    g_trayIcon.UpdateMenuState(
        g_windowVisible,
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
    
    // 注销窗口类
    ::UnregisterClass(L"DynamicIsland", GetModuleHandle(nullptr));
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
            // 灵动岛配置
            ImVec2 size = g_islandExpanded ? ImVec2(500.0f, 150.0f) : ImVec2(400.0f, 80.0f);
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
                }
                
                // 恢复文本颜色
                ImGui::PopStyleColor();
            }
            
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }
        

        
        // 渲染
        ImGui::Render();
        
        // 检查是否有绘制数据
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->CmdListsCount > 0) {
            if (frameCount < 10) {
                LOG_INFO("Frame %d: CmdListsCount=%d, TotalVtxCount=%d", 
                    frameCount, drawData->CmdListsCount, drawData->TotalVtxCount);
            }
        } else if (frameCount < 10) {
            LOG_INFO("Frame %d: No draw data!", frameCount);
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
