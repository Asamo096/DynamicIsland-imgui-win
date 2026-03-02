#pragma once

#include <windows.h>
#include <functional>
#include <string>

// 托盘菜单命令ID
enum class TrayCommand : UINT {
    SHOW_HIDE = 1001,
    EXPAND_PANEL = 1002,
    PERF_POWER_SAVE = 1101,
    PERF_BALANCED = 1102,
    PERF_PERFORMANCE = 1103,
    POS_TOP_CENTER = 1201,
    POS_TOP_LEFT = 1202,
    POS_FOLLOW_TASKBAR = 1203,
    STARTUP_TOGGLE = 1301,
    SETTINGS = 1401,
    EXIT = 1501
};

// 性能模式
enum class PerformanceMode {
    POWER_SAVE,   // 省电: 5秒刷新
    BALANCED,     // 平衡: 1秒刷新 (默认)
    PERFORMANCE   // 性能: 0.5秒刷新
};

// 位置设置
enum class IslandPosition {
    TOP_CENTER,       // 顶部居中 (默认)
    TOP_LEFT,         // 顶部左侧
    FOLLOW_TASKBAR    // 跟随任务栏
};

// 托盘图标管理器
class TrayIcon {
public:
    using StateChangeCallback = std::function<void()>;
    using PerformanceCallback = std::function<void(PerformanceMode)>;
    using PositionCallback = std::function<void(IslandPosition)>;
    using BoolCallback = std::function<void(bool)>;
    
    TrayIcon();
    ~TrayIcon();
    
    // 初始化/清理
    bool Initialize(HWND hwnd, UINT callbackMessage);
    void Shutdown();
    
    // 设置回调
    void SetShowHideCallback(StateChangeCallback cb) { onShowHide = cb; }
    void SetExpandCallback(StateChangeCallback cb) { onExpand = cb; }
    void SetPerformanceCallback(PerformanceCallback cb) { onPerformanceChange = cb; }
    void SetPositionCallback(PositionCallback cb) { onPositionChange = cb; }
    void SetStartupCallback(BoolCallback cb) { onStartupToggle = cb; }
    void SetSettingsCallback(StateChangeCallback cb) { onSettings = cb; }
    void SetExitCallback(StateChangeCallback cb) { onExit = cb; }
    
    // 处理托盘消息
    void HandleMessage(WPARAM wParam, LPARAM lParam);
    
    // 更新菜单状态
    void UpdateMenuState(bool isVisible, PerformanceMode perfMode, 
                         IslandPosition position, bool startupEnabled);
    
    // 显示气泡提示
    void ShowBalloonTip(const std::string& title, const std::string& message, 
                        DWORD infoFlags = NIIF_INFO);
    
    // 修改图标 (可选: 根据CPU使用率改变颜色)
    void SetIcon(HICON hIcon);
    void SetIconByCPUUsage(float usage);
    
private:
    void ShowContextMenu();
    void CreateMenu();
    void DestroyMenu();
    
    HWND hwnd = nullptr;
    UINT callbackMsg = 0;
    NOTIFYICONDATA nid{};
    HMENU hMenu = nullptr;
    HMENU hPerfMenu = nullptr;
    HMENU hPosMenu = nullptr;
    
    // 回调
    StateChangeCallback onShowHide;
    StateChangeCallback onExpand;
    PerformanceCallback onPerformanceChange;
    PositionCallback onPositionChange;
    BoolCallback onStartupToggle;
    StateChangeCallback onSettings;
    StateChangeCallback onExit;
    
    // 状态
    bool isVisible = true;
    PerformanceMode currentPerfMode = PerformanceMode::BALANCED;
    IslandPosition currentPosition = IslandPosition::TOP_CENTER;
    bool startupEnabled = true;
};

// 全局实例 (可选)
extern TrayIcon g_trayIcon;