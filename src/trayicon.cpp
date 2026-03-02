#include "trayicon.h"
#include <shellapi.h>
#include <strsafe.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

TrayIcon g_trayIcon;

// 从PNG文件加载图标
HICON LoadIconFromPNG(const wchar_t* path) {
    HICON hIcon = nullptr;
    
    // 初始化GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    
    try {
        // 加载PNG文件
        Bitmap bitmap(path);
        if (bitmap.GetLastStatus() == Ok) {
            // 创建图标
            bitmap.GetHICON(&hIcon);
        }
    } catch (...) {
        // 异常处理
    }
    
    // 关闭GDI+
    GdiplusShutdown(gdiplusToken);
    
    return hIcon;
}

TrayIcon::TrayIcon() {}

TrayIcon::~TrayIcon() {
    Shutdown();
}

bool TrayIcon::Initialize(HWND hwnd, UINT callbackMessage) {
    this->hwnd = hwnd;
    this->callbackMsg = callbackMessage;
    
    // 初始化NOTIFYICONDATA
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = callbackMessage;
    
    // 尝试从PNG文件加载图标
    HICON hIcon = LoadIconFromPNG(L"icon.png");
    if (hIcon) {
        nid.hIcon = hIcon;
    } else {
        // 如果加载失败，使用默认图标
        nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    
    StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), TEXT("DynamicIsland System Monitor"));
    
    // 添加托盘图标
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        // 清理图标
        if (hIcon) DestroyIcon(hIcon);
        return false;
    }
    
    // 设置NIM_SETVERSION以使用现代气泡提示
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &nid);
    
    CreateMenu();
    return true;
}

void TrayIcon::Shutdown() {
    DestroyMenu();
    if (hwnd) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        // 释放图标资源
        if (nid.hIcon) {
            DestroyIcon(nid.hIcon);
            nid.hIcon = nullptr;
        }
        hwnd = nullptr;
    }
}

void TrayIcon::CreateMenu() {
    // 创建主菜单
    hMenu = CreatePopupMenu();
    
    // 展开面板
    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::EXPAND_PANEL, TEXT("展开面板"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    
    // 性能模式子菜单
    hPerfMenu = CreatePopupMenu();
    AppendMenu(hPerfMenu, MF_STRING, (UINT)TrayCommand::PERF_POWER_SAVE, TEXT("省电 (5秒刷新)"));
    AppendMenu(hPerfMenu, MF_STRING, (UINT)TrayCommand::PERF_BALANCED, TEXT("平衡 (1秒刷新)"));
    AppendMenu(hPerfMenu, MF_STRING, (UINT)TrayCommand::PERF_PERFORMANCE, TEXT("性能 (0.5秒刷新)"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hPerfMenu, TEXT("性能模式"));
    

    
    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
    
    // 开机启动
    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::STARTUP_TOGGLE, TEXT("开机启动"));
    
    // 退出
    AppendMenu(hMenu, MF_STRING, (UINT)TrayCommand::EXIT, TEXT("退出"));
}

void TrayIcon::DestroyMenu() {
    if (hMenu) {
        ::DestroyMenu(hMenu);
        hMenu = nullptr;
    }
    hPerfMenu = nullptr;
    hPosMenu = nullptr;
}

void TrayIcon::ShowContextMenu() {
    if (!hMenu || !hwnd) return;
    
    // 更新菜单状态
    UpdateMenuState(isVisible, currentPerfMode, currentPosition, startupEnabled);
    
    // 获取鼠标位置
    POINT pt;
    GetCursorPos(&pt);
    
    // 设置前台窗口以正确显示菜单
    SetForegroundWindow(hwnd);
    
    // 显示菜单
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, 
                   pt.x, pt.y, 0, hwnd, nullptr);
    
    // 必要的PostMessage以解决菜单不消失的问题
    PostMessage(hwnd, WM_NULL, 0, 0);
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    if (LOWORD(lParam) == WM_RBUTTONUP) {
        ShowContextMenu();
    } else if (LOWORD(lParam) == WM_LBUTTONUP) {
        // 左键点击切换显示/隐藏
        if (onShowHide) onShowHide();
    } else if (LOWORD(lParam) == WM_COMMAND || HIWORD(lParam) == 0) {
        // 处理菜单命令
        UINT cmd = LOWORD(wParam);
        switch ((TrayCommand)cmd) {
            case TrayCommand::EXPAND_PANEL:
                if (onExpand) onExpand();
                break;
            case TrayCommand::PERF_POWER_SAVE:
                currentPerfMode = PerformanceMode::POWER_SAVE;
                if (onPerformanceChange) onPerformanceChange(currentPerfMode);
                break;
            case TrayCommand::PERF_BALANCED:
                currentPerfMode = PerformanceMode::BALANCED;
                if (onPerformanceChange) onPerformanceChange(currentPerfMode);
                break;
            case TrayCommand::PERF_PERFORMANCE:
                currentPerfMode = PerformanceMode::PERFORMANCE;
                if (onPerformanceChange) onPerformanceChange(currentPerfMode);
                break;
            case TrayCommand::STARTUP_TOGGLE:
                startupEnabled = !startupEnabled;
                if (onStartupToggle) onStartupToggle(startupEnabled);
                break;
            case TrayCommand::EXIT:
                if (onExit) onExit();
                break;
        }
    }
}

void TrayIcon::UpdateMenuState(bool visible, PerformanceMode perfMode, 
                                IslandPosition position, bool startup) {
    isVisible = visible;
    currentPerfMode = perfMode;
    currentPosition = position;
    startupEnabled = startup;
    
    if (!hMenu) return;
    
    // 更新性能模式勾选
    CheckMenuItem(hPerfMenu, (UINT)TrayCommand::PERF_POWER_SAVE, 
                  MF_BYCOMMAND | (perfMode == PerformanceMode::POWER_SAVE ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hPerfMenu, (UINT)TrayCommand::PERF_BALANCED, 
                  MF_BYCOMMAND | (perfMode == PerformanceMode::BALANCED ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hPerfMenu, (UINT)TrayCommand::PERF_PERFORMANCE, 
                  MF_BYCOMMAND | (perfMode == PerformanceMode::PERFORMANCE ? MF_CHECKED : MF_UNCHECKED));
    

    
    // 更新开机启动勾选
    CheckMenuItem(hMenu, (UINT)TrayCommand::STARTUP_TOGGLE, 
                  MF_BYCOMMAND | (startup ? MF_CHECKED : MF_UNCHECKED));
}

void TrayIcon::ShowBalloonTip(const std::string& title, const std::string& message, 
                               DWORD infoFlags) {
    if (!hwnd) return;
    
    NOTIFYICONDATA nidCopy = nid;
    nidCopy.uFlags |= NIF_INFO;
    
    // 转换为宽字符
    wchar_t wtitle[64] = {};
    wchar_t wmessage[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wtitle, 64);
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wmessage, 256);
    
    // 复制到宽字符数组
    wcsncpy_s(nidCopy.szInfoTitle, wtitle, _TRUNCATE);
    wcsncpy_s(nidCopy.szInfo, wmessage, _TRUNCATE);
    
    nidCopy.dwInfoFlags = infoFlags;
    nidCopy.uTimeout = 3000;
    
    Shell_NotifyIcon(NIM_MODIFY, &nidCopy);
}

void TrayIcon::SetIcon(HICON hIcon) {
    if (!hwnd || !hIcon) return;
    
    nid.hIcon = hIcon;
    nid.uFlags |= NIF_ICON;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void TrayIcon::SetIconByCPUUsage(float usage) {
    // 根据CPU使用率设置不同图标 (绿/黄/红)
    // 这里简化处理，实际可以加载不同颜色的图标资源
    HICON hIcon = nullptr;
    if (usage < 50.0f) {
        hIcon = LoadIcon(nullptr, IDI_INFORMATION); // 绿色/低负载
    } else if (usage < 80.0f) {
        hIcon = LoadIcon(nullptr, IDI_WARNING);     // 黄色/中负载
    } else {
        hIcon = LoadIcon(nullptr, IDI_ERROR);       // 红色/高负载
    }
    SetIcon(hIcon);
}
