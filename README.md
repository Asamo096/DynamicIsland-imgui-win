# DynamicIsland for Windows 🪟

**DynamicIsland-imgui-win** 是一个基于 [Dear ImGui](https://github.com/ocornut/imgui) 和 Direct3D11 的 Windows 桌面应用，模拟 macOS 上的 "灵动岛"（Dynamic Island）效果并在其中显示系统监控信息。

***

## 🚀 项目简介

该程序在桌面顶层创建一个始终可见的透明窗口，可在固定位置显示或展开为信息面板。面板内可以实时展示：

- CPU / 每核利用率与频率
- GPU 利用率、显存、温度（支持 NVIDIA/AMD/Intel）
- 内存使用情况
- 电池状态与剩余时间
- （可选）网络带宽统计
- 时间（可显示秒）

此外支持：

- 托盘图标与右键菜单，用于切换显示、性能模式、开机启动和退出
- 自动隐藏、动画展开/收起、位置与尺寸可配置
- 开机启动管理（通过 Windows 任务计划程序）
- 简单的 JSON 配置文件，自行编辑或通过 UI 修改
- 支持暗色/亮色主题、磨砂/液体玻璃样式、自定义字体/颜色/圆角等

***

## 🧩 结构说明

```
/                # 项目根
├─ CMakeLists.txt
├─ include/imgui/      # Dear ImGui 源码（作为子模块/拷贝）
├─ src/                # 应用源代码
│   ├ config.*
│   ├ sysinfo.*
│   ├ scheduler.*
│   ├ trayicon.*
│   └ main.cpp
├─ assets/             # 字体、图标等资源
├─ LICENSE             # AGPL‑3.0
└─ README.md           # 本文档
```

主要模块：

- `config`：管理 `config.json`，定义配置结构体。
- `sysinfo`：后台线程采集系统指标并暴露给 UI。
- `trayicon`：托盘图标与菜单交互封装。
- `scheduler`：封装对 Windows 任务计划程序的操作，用于注册开机启动。
- `main`：程序入口，初始化 ImGui / D3D11 /窗口/逻辑循环。

***

## 🛠 构建要求

- **操作系统**：Windows 10/11（32/64 位）。
- **工具链**：
  - CMake ≥ 3.20
  - Ninja（或其他生成器）
  - MinGW‑w64/GCC（`g++`），重点是能够链接静态库
  - 或者 Visual Studio 2019+（仅需调整 `CMakeLists.txt`）
- **依赖库**：
  - Direct3D 11（系统自带）
  - Windows SDK（包含 DWM、taskschd、Pdh、iphlpapi 等）
  - ImGui 已随仓库提供，无需额外下载。

> 目前 `CMakeLists.txt` 硬编码了 MinGW 路径；在其他环境下请移除或修改相应变量。

***

## 📦 构建步骤

```powershell
# 在仓库根目录执行
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

生成的可执行文件 `DynamicIsland.exe` 会包含在 `build/` 目录中，构建后自动复制 `assets/`。

调试构建：

```powershell
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

***

## ⚙️ 配置说明

程序使用 `config.json`（默认与可执行文件同目录）保存设置。第一次运行会自动创建默认配置，格式如下：

```json
{
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
    "corner_radius": 20.0,
    "shadow_enabled": true,
    "blur_enabled": true,
    "font_size": 16,
    "font_family": "Noto Sans CJK SC",
    "style": "frosted"
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
    "start_minimized": false,
    "silent_mode": false,
    "game_mode_detection": true,
    "notification_enabled": true,
    "max_notifications": 5
  }
}
```

> 所有字段在代码 `include/config.h` 定义。编辑完成后重启程序或通过 UI（计划中）生效。

***

## 🚗 运行及使用

双击 `DynamicIsland.exe` 或通过命令行启动，可添加参数：

```
DynamicIsland.exe            # 带 UI
DynamicIsland.exe /background # 仅启动后台监控（不显示窗口）
```

程序会在系统托盘显示图标。右键单击图标可访问菜单：

- 展开/收起面板
- 切换性能模式（省电/平衡/性能）
- 切换开机启动
- 退出

左键单击图标可以快速显示/隐藏灵动岛。

窗口本身支持拖动（抓住任意空白区域），并通过配置改变位置。

***

## 🔧 开发说明

- UI 界面依赖 ImGui，渲染使用 `imgui_impl_win32.cpp` 和 `imgui_impl_dx11.cpp`。
- 图标资源位于 `assets/`，程序运行时会从该目录加载 `icon.png`。
- 日志写入 `dynamicisland.log`，用于调试。
- 系统监控通过 PDH、WMI、NVML 动态链接获取数据。
- 任务计划器接口封装对 COM 的使用，可注册隐藏启动任务。

欢迎阅读源代码并提出 issue 或贡献 PR。

***

## 📡 功能一览

| 模块  | 描述                               |
| --- | -------------------------------- |
| CPU | 总/单核利用率、频率、开机时长                  |
| GPU | 利用率、显存占用、温度、型号（NVIDIA/AMD/Intel） |
| 内存  | 使用率、总/已用/可用大小                    |
| 电池  | 电量、充电状态、剩余时间、节能模式                |
| 显示  | 刷新率、分辨率、DPI                      |
| 网络  | 下载/上传速度及总量（可启用）                  |
| 托盘  | 菜单、模式切换、显示/隐藏、开机启动               |
| 配置  | 位置、样式、主题、更新间隔等全面自定义              |

***

## 📦 发行与安装

此项目不提供安装程序。只需将可执行文件及 `assets/` 目录拷贝到任意位置；首次运行会在该目录生成 `config.json`。

如需卸载，删除程序文件即可，注册的启动任务可通过程序右键菜单或 Windows 任务计划程序移除。

***

## ✨ 贡献

1. Fork 本仓库并创建新分支
2. 编写代码并确保保持项目结构清晰
3. 使用 CMake/Ninja 构建并在 Windows 下充分测试
4. 提交 PR 说明更改目的与影响

***

## 感谢使用 🎉

欢迎反馈 bug、建议新功能或参与开发！
