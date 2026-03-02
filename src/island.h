#pragma once

#include "imgui.h"
#include "trayicon.h"
#include <functional>
#include <chrono>

// 状态机枚举 - 匹配README规格
enum class IslandState {
    IDLE,       // 空闲：最小胶囊，显示时间/电量图标
    COMPACT,    // 紧凑：横向扩展，显示单条通知或关键状态
    EXPANDED,   // 展开：纵向展开，完整面板
    SETTINGS,   // 设置界面
    COUNT
};

// 内容类型 - 匹配README规格
enum class IslandContent {
    NONE,
    SYSTEM,         // 系统状态（CPU/GPU/内存）
    NOTIFICATION,   // 应用通知
    MEDIA,          // 媒体播放控制
    CALL            // 来电/会议提醒
};

// 动画属性
struct AnimationState {
    float currentWidth = 120.0f;
    float currentHeight = 40.0f;
    float targetWidth = 120.0f;
    float targetHeight = 40.0f;
    float opacity = 0.8f;
    float cornerRadius = 20.0f;
    float hoverScale = 1.0f;
    
    // 内容滚动偏移
    float contentOffsetY = 0.0f;
    float targetContentOffsetY = 0.0f;
};

// 灵动岛核心类
class DynamicIsland {
public:
    DynamicIsland();
    ~DynamicIsland();
    
    // 初始化
    void Initialize();
    void Shutdown();
    
    // 每帧更新
    void Update(float deltaTime);
    
    // 渲染
    void Draw();
    
    // 状态控制
    void SetState(IslandState state);
    IslandState GetState() const { return currentState; }
    void SetContent(IslandContent content) { currentContent = content; }
    IslandContent GetContent() const { return currentContent; }
    
    // 尺寸查询
    float GetCurrentWidth() const { return animState.currentWidth; }
    float GetCurrentHeight() const { return animState.currentHeight; }
    float GetTargetWidth() const { return animState.targetWidth; }
    float GetTargetHeight() const { return animState.targetHeight; }
    
    // 可见性控制
    void SetVisible(bool visible);
    bool IsVisible() const { return isVisible; }
    
    // 自动隐藏控制
    void SetAutoHideEnabled(bool enabled) { autoHideEnabled = enabled; }
    bool IsAutoHideEnabled() const { return autoHideEnabled; }
    void ResetAutoHideTimer();
    
    // 性能模式
    void SetPerformanceMode(PerformanceMode mode);
    PerformanceMode GetPerformanceMode() const { return perfMode; }
    
    // 位置设置
    void SetPosition(IslandPosition pos);
    IslandPosition GetPosition() const { return position; }
    ImVec2 CalculatePosition(float windowWidth, float windowHeight) const;
    
    // 设置回调
    void SetStateChangeCallback(std::function<void(IslandState)> cb) { onStateChange = cb; }
    
private:
    // 各状态绘制函数
    void DrawIdle();
    void DrawCompact();
    void DrawExpanded();
    void DrawSettings();
    
    // 绘制辅助函数
    void PrepareWindow(const char* name, ImVec2 size);
    void DrawRoundedBackground(ImDrawList* dl, ImVec2 pos, ImVec2 size, float radius, ImU32 color);
    void DrawProgressBar(ImDrawList* dl, ImVec2 pos, ImVec2 size, float progress, ImU32 color);
    void DrawCPUGraph(ImDrawList* dl, ImVec2 pos, ImVec2 size);
    
    // 动画更新
    void UpdateAnimation(float deltaTime);
    
    // 自动隐藏检查
    void CheckAutoHide(float deltaTime);
    
    // 处理输入
    void HandleInput();
    
    // 状态
    IslandState currentState = IslandState::IDLE;
    IslandState previousState = IslandState::IDLE;
    IslandContent currentContent = IslandContent::SYSTEM;
    AnimationState animState;
    
    // 可见性
    bool isVisible = true;
    bool isWindowHovered = false;
    bool isContentHovered = false;
    
    // 自动隐藏
    bool autoHideEnabled = true;
    float autoHideTimer = 0.0f;
    float autoHideDelay = 5.0f;
    
    // 性能模式
    PerformanceMode perfMode = PerformanceMode::BALANCED;
    
    // 位置
    IslandPosition position = IslandPosition::TOP_CENTER;
    
    // CPU历史数据 (用于图表)
    static constexpr int MAX_CPU_HISTORY = 64;
    float cpuHistory[MAX_CPU_HISTORY] = {};
    int cpuHistoryPos = 0;
    
    // 回调
    std::function<void(IslandState)> onStateChange;
    
    // 时间戳
    std::chrono::steady_clock::time_point lastUpdateTime;
};

// 全局实例
extern DynamicIsland g_island;