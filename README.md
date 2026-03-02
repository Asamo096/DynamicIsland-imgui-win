以下是 DynamicIsland 完整项目的逻辑架构与框架详解，按模块分层说明。
整体架构图
plain
复制
┌─────────────────────────────────────────────────────────┐
│                    用户交互层 (Input)                     │
│         鼠标悬停/点击/拖拽 │ 系统托盘右键菜单              │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                   灵动岛核心 (Island)                     │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌──────────┐   │
│  │  Idle   │→│ Compact │→│Expanded │←│ Settings │   │
│  │  空闲态  │  │ 紧凑通知 │  │ 展开面板 │  │  设置界面  │   │
│  └────┬────┘  └────┬────┘  └────┬────┘  └──────────┘   │
│       └─────────────┴───────────┘                       │
│              状态机管理 + 动画插值系统                     │
│         (ImLerp实现尺寸/位置/透明度平滑过渡)               │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                   系统数据层 (SysInfo)                    │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌───────┐ │
│  │  CPU   │ │  GPU   │ │ Memory │ │ Battery│ │Network│ │
│  │ 监控线程│ │ 监控线程│ │ 定时查询 │ │ 事件监听 │ │ 流量统计│ │
│  └────────┘ └────────┘ └────────┘ └────────┘ └───────┘ │
│              统一数据缓存池 (线程安全)                      │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                   平台服务层 (Platform)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  TaskScheduler│  │   TrayIcon   │  │  WindowMgmt  │  │
│  │  任务计划程序  │  │   系统托盘    │  │  窗口管理    │  │
│  │  (开机自启动)  │  │  (右键菜单)   │  │ (置顶/穿透)  │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────┐
│                   渲染后端 (Render)                       │
│         DirectX 11 │ Win32 窗口 │ ImGui DrawList         │
│         (阴影/圆角/毛玻璃效果自定义着色器)                  │
└─────────────────────────────────────────────────────────┘
模块详解
1. main.cpp（程序入口与生命周期）
职责：程序启动、初始化、主循环、资源清理
启动流程：
plain
复制
WinMain 入口
    ↓
解析命令行参数 (/background = 静默启动)
    ↓
单实例检测 (Mutex防止多开)
    ↓
创建 Win32 无边框窗口 (WS_EX_LAYERED | WS_EX_NOACTIVATE)
    ↓
初始化 DirectX 11 渲染器
    ↓
初始化 ImGui (字体加载、样式配置)
    ↓
初始化各模块: Island → SysInfo → TrayIcon → TaskScheduler
    ↓
进入消息循环 (PeekMessage + ImGui 渲染)
    ↓
收到 WM_QUIT
    ↓
清理资源、保存配置、退出
关键设计：
单实例机制：使用命名 Mutex Global\DynamicIsland，检测到已存在则激活现有实例
命令行参数：/background 表示由任务计划程序启动，不显示启动动画
消息循环：不用阻塞式 GetMessage，而用 PeekMessage 实现 60FPS 渲染循环
2. island.h / island.cpp（灵动岛核心）
职责：UI 状态管理、动画系统、内容布局、交互响应
状态机设计
cpp
复制
enum class IslandState {
    IDLE,       // 空闲：最小胶囊，显示时间/电量图标
    COMPACT,    // 紧凑：横向扩展，显示单条通知或关键状态
    EXPANDED,   // 展开：纵向展开，完整面板
    SETTINGS    // 设置：独立界面（可选，或作为Expanded子页面）
};

enum class IslandContent {
    NONE,       // 无内容，纯装饰
    SYSTEM,     // 系统状态（CPU/GPU/内存）
    NOTIFICATION, // 应用通知
    MEDIA,      // 媒体播放控制
    CALL        // 来电/会议提醒（模拟）
};
动画系统
表格
属性	动画方式	说明
宽度	ImLerp(current, target, dt * 12)	快速响应
高度	ImLerp(current, target, dt * 10)	稍慢，有层次感
圆角半径	随高度插值	展开时变方，收缩时变圆
透明度	ImLerp	显示/隐藏过渡
内容偏移	弹簧物理	列表滚动惯性
内容布局（Expanded 态）
plain
复制
┌─────────────────────────────┐
│  [时间]  [日期]        [X]  │  ← 标题栏
├─────────────────────────────┤
│  ┌─────┐  CPU: 45%          │
│  │ 图表 │  GPU: 32%  ▓▓▓▓░  │  ← 实时性能区
│  │ 曲线 │  MEM: 60%  ▓▓▓▓▓░ │
│  └─────┘  NET: ↓1.2MB/s     │
├─────────────────────────────┤
│  [电量] 85%  [预计2小时]     │  ← 电源信息
│  [电源模式: 平衡] [🔌/🔋]    │
├─────────────────────────────┤
│  [刷新率] 144Hz  [分辨率]    │  ← 显示信息
├─────────────────────────────┤
│  通知列表 (最近3条)          │  ← 通知中心
│  • 微信: 新消息              │
│  • 邮件: 会议提醒            │
├─────────────────────────────┤
│  [勿扰] [设置] [退出]        │  ← 快捷操作
└─────────────────────────────┘
交互映射
表格
操作	响应
鼠标悬停 Idle	轻微放大 (1.05x)，显示工具提示
点击 Idle	过渡到 Expanded
鼠标悬停 Compact	暂停自动收缩计时器
点击 Compact	根据内容类型展开详情或执行动作
Expanded 内滚轮	滚动内容列表
Expanded 外点击	收缩回 Idle
右键点击	托盘菜单（最小化/设置/退出）
3. sysinfo.h / sysinfo.cpp（系统信息监控）
职责：多线程数据采集、统一缓存、事件驱动更新
架构设计
plain
复制
┌─────────────────────────────────────┐
│           SysInfoManager            │
│  (单例，主线程安全访问接口)            │
├─────────────────────────────────────┤
│  ┌─────────┐ ┌─────────┐ ┌────────┐ │
│  │ CPU线程 │ │ GPU线程 │ │ 电池监听│ │  ← 后台工作线程
│  │ (1s)   │ │ (1s)   │ │ (事件) │ │
│  └────┬────┘ └────┬────┘ └───┬────┘ │
│       └────────────┴─────────┘       │
│              数据锁 (shared_mutex)    │
│       ┌────────────┴─────────┐       │
│       ↓                      ↓       │
│  ┌─────────┐            ┌─────────┐  │
│  │ 原始数据池 │            │ 统计计算  │  │
│  │ (原始值) │            │ (平均值) │  │
│  └─────────┘            └─────────┘  │
└─────────────────────────────────────┘
数据结构设计
cpp
复制
struct CPUInfo {
    float usage_percent;        // 总占用率
    float usage_per_core[32];   // 各核心占用
    float frequency_ghz;        // 当前频率
    uint64_t uptime_seconds;    // 运行时间
    std::chrono::steady_clock::time_point timestamp;
};

struct GPUInfo {
    float usage_percent;        // GPU 利用率
    float memory_used_mb;       // 显存使用
    float memory_total_mb;      // 显存总量
    float temperature;          // 温度 (°C)
    std::string name;           // 显卡型号
};

struct BatteryInfo {
    int percent;                // 电量百分比
    bool is_charging;           // 是否充电
    bool is_plugged;            // 是否接通电源
    int remaining_minutes;      // 预计剩余时间
    std::string power_mode;     // 电源模式 (节能/平衡/性能)
};

struct DisplayInfo {
    int refresh_rate_hz;        // 刷新率
    int resolution_x;           // 分辨率宽
    int resolution_y;           // 分辨率高
    float dpi_scale;            // DPI 缩放比例
};
实现技术
表格
数据	Windows API	说明
CPU	PdhOpenQuery + PdhAddCounter (\Processor(_Total)% Processor Time)	性能计数器，需初始化后等待1秒获取有效值
GPU	NVIDIA: nvmlDeviceGetUtilizationRates (NVML)
AMD: ADL2_Overdrive5_CurrentActivity_Get (ADL)
Intel: GPA 或回退到 DXGI	优先检测 vendor，动态加载对应 DLL
内存	GlobalMemoryStatusEx	简单直接
电池	GetSystemPowerStatus + RegisterPowerSettingNotification	事件驱动，变化时即时回调
刷新率	EnumDisplaySettings 或 DXGI_OUTPUT_DESC	启动时获取，监听 WM_DISPLAYCHANGE
网络	GetIfTable (MIB_IFROW)	计算每秒字节差值得速率
线程安全：
后台线程只写 raw_data_
主线程通过 GetCPUInfo() 等接口读，内部加共享锁
使用 std::atomic 存储简单状态标志
4. scheduler.h / scheduler.cpp（任务计划程序）
职责：管理开机自启动，无需用户手动配置
功能设计
cpp
复制
class TaskScheduler {
public:
    // 检查是否已注册
    bool IsRegistered();
    
    // 注册开机启动（首次运行或用户开启时调用）
    bool Register(
        bool delayStart = true,      // 延迟30秒
        bool acPowerOnly = false,    // 仅电源模式
        bool hidden = true           // 隐藏窗口启动
    );
    
    // 取消注册（用户关闭开机启动时）
    bool Unregister();
    
    // 更新配置（修改延迟时间、条件等）
    bool UpdateConfig(const TaskConfig& config);
};
任务计划程序 XML 配置（内部生成）
xml
复制
<Task>
  <RegistrationInfo>
    <Description>DynamicIsland System Monitor</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Delay>PT30S</Delay>  <!-- 延迟30秒 -->
    </LogonTrigger>
  </Triggers>
  <Principals>
    <LogonType>InteractiveToken</LogonType>
    <RunLevel>HighestAvailable</RunLevel>
  </Principals>
  <Settings>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <Hidden>true</Hidden>  <!-- 隐藏任务 -->
  </Settings>
  <Actions>
    <Exec>
      <Command>C:\Path\To\DynamicIsland.exe</Command>
      <Arguments>/background</Arguments>
    </Exec>
  </Actions>
</Task>
实现方式：
使用 COM 接口 ITaskService、ITaskFolder、ITaskDefinition
需要管理员权限（首次注册时 UAC 提权）
程序内提供开关：设置界面 → 开机启动 [开/关]
5. trayicon.h / trayicon.cpp（系统托盘）
职责：提供程序入口、状态指示、快捷操作
功能设计
plain
复制
右键点击托盘图标
    ├── 显示/隐藏灵动岛  (切换可见性)
    ├── 展开面板        (直接打开 Expanded 态)
    ├── ─────────────   (分隔线)
    ├── 性能模式        (子菜单)
    │       ├── 省电    (降低刷新率到5秒)
    │       ├── 平衡    (默认1秒)
    │       └── 性能    (实时刷新，0.5秒)
    ├── 位置设置        (子菜单)
    │       ├── 顶部居中 (默认)
    │       ├── 顶部左侧
    │       └── 跟随任务栏 (多显示器适配)
    ├── ─────────────
    ├── 开机启动 [✓]    (勾选状态)
    ├── 设置...         (打开配置文件或 Expanded 设置页)
    └── 退出            (完全关闭程序)
实现技术
Shell_NotifyIcon (NIM_ADD/NIM_MODIFY/NIM_DELETE)
自定义消息 WM_TRAYICON 处理点击
图标动态变化（可选）：根据 CPU 占用率改变颜色（绿/黄/红）
配置文件设计（config.json）
JSON
复制
{
    "version": "1.0",
    "island": {
        "position": "top-center",
        "offset_x": 0,
        "offset_y": 20,
        "idle_width": 120,
        "idle_height": 40,
        "expanded_width": 380,
        "expanded_height": 450,
        "animation_speed": 12.0,
        "auto_hide_delay": 5.0,
        "show_seconds": false
    },
    "appearance": {
        "theme": "dark",
        "accent_color": "#0078D4",
        "opacity": 0.95,
        "corner_radius": 20,
        "shadow_enabled": true,
        "blur_enabled": true,
        "font_size": 16,
        "font_family": "Noto Sans CJK SC"
    },
    "system": {
        "update_interval_ms": 1000,
        "cpu_enabled": true,
        "gpu_enabled": true,
        "memory_enabled": true,
        "battery_enabled": true,
        "network_enabled": false
    },
    "behavior": {
        "start_with_windows": true,
        "start_minimized": true,
        "silent_mode": false,
        "game_mode_detection": true,
        "notification_enabled": true,
        "max_notifications": 5
    }
}
关键设计决策
表格
决策点	选择	理由
单例模式	是	系统级悬浮面板，多实例无意义
多线程采集	是	避免 WMI/PDH 查询阻塞 UI 线程
DirectX 11	是	Win10 标配，性能优于 OpenGL/GDI
配置文件	JSON	易读易改，支持中文
日志系统	spdlog (可选)	调试时写入 %APPDATA%/logs
更新检查	GitHub API	可选功能，不强制
开发阶段规划
表格
阶段	目标	验证标准
M1	空窗口 + ImGui 初始化	显示 ImGui Demo 窗口，无控制台
M2	胶囊绘制 + 状态切换	点击切换 Idle/Expanded，有动画
M3	系统数据接入	CPU/内存实时显示，数据准确
M4	托盘 + 自启动	开机自启，托盘可控
M5	通知系统	能显示模拟通知，视觉完整
M6	polish	阴影/毛玻璃/字体/配置持久化