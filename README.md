# DynamicIsland-imgui-win

一个模仿苹果灵动岛设计的 Windows 系统监控工具，使用 ImGui 和 DirectX 11 开发。

## 功能特性

### 核心功能
- **实时系统监控**：CPU、内存、GPU、网络、电池等系统信息
- **灵动岛界面**：模仿苹果灵动岛的动态交互界面
- **状态栏托盘**：系统托盘图标，提供快捷操作
- **设置系统**：独立的设置窗口，支持分类配置
- **文件中转站**：支持文件拖放管理（可启用）

### 界面特性
- **透明效果**：半透明背景，融入桌面环境
- **平滑动画**：展开/收起动画效果
- **响应式设计**：根据系统状态自动调整
- **主题支持**：预留主题配置接口

## 系统要求

- **操作系统**：Windows 10 或 Windows 11
- **架构**：64位系统
- **DirectX**：DirectX 11 或更高版本
- **内存**：至少 512MB 可用内存
- **CPU**：支持 SSE2 指令集的处理器

## 安装方法

### 方法 1：使用预编译版本
1. 从 [Releases](https://github.com/Asamo096/DynamicIsland-imgui-win/releases) 下载最新版本
2. 解压到任意目录
3. 运行 `DynamicIsland.exe` 即可

### 方法 2：从源码构建
1. **安装依赖**：
   - Visual Studio 2019 或更高版本
   - CMake 3.16 或更高版本
   - DirectX SDK

2. **克隆仓库**：
   ```bash
   git clone https://github.com/Asamo096/DynamicIsland-imgui-win.git
   cd DynamicIsland-imgui-win
   ```

3. **构建项目**：
   ```bash
   mkdir build
   cd build
   cmake .. -G "Visual Studio 16 2019"
   cmake --build . --config Release
   ```

4. **运行程序**：
   ```bash
   ./Release/DynamicIsland.exe
   ```

## 使用方法

### 基本操作
- **左键点击**：展开/收起灵动岛
- **右键点击**：打开系统托盘菜单
- **ESC 键**：关闭设置窗口

### 系统托盘菜单
- **展开面板**：展开灵动岛详细信息
- **设置**：打开设置窗口
- **性能模式**：
  - 省电 (5秒刷新)
  - 平衡 (1秒刷新)
  - 性能 (0.5秒刷新)
- **开机启动**：设置是否随系统启动
- **退出**：退出程序

### 快捷操作
- **Ctrl+Shift+Z**：快速退出程序

## 设置窗口

设置窗口包含以下分类：
- **通用**：开机启动、刷新频率等基本设置
- **外观**：主题、动画效果等界面设置
- **通知**：系统通知配置
- **文件中转站**：文件管理设置（需启用）
- **高级**：调试选项
- **关于**：版本信息和功能列表

## 文件中转站功能

文件中转站功能默认禁用，可通过修改 `src/main.cpp` 中的宏启用：

```cpp
// 文件中转站功能开关 (0=禁用, 1=启用)
#define USE_FILE_TRANSFER 1
```

**功能特性**：
- 支持文件拖放到灵动岛
- 支持文件的复制、移动、删除操作
- 支持文件预览（图片、文本）
- 支持文件拖出到其他应用

## 配置文件

程序会在运行目录生成配置文件 `config.json`，可手动编辑调整参数。

## 常见问题

### 程序无法启动
- 检查系统是否满足最低要求
- 确保 DirectX 11 已正确安装
- 检查是否有其他程序占用端口

### 灵动岛不显示
- 检查是否被其他窗口遮挡
- 检查系统托盘是否有程序图标
- 尝试重启程序

### 鼠标操作问题
- 灵动岛只在显示区域内捕获鼠标
- 非显示区域的鼠标事件会透传给下层窗口

## 开发指南

### 项目结构
- `src/`：源代码目录
- `include/`：第三方库（ImGui）
- `build/`：构建输出目录

### 主要文件
- `src/main.cpp`：主程序入口
- `src/sysinfo.cpp`：系统信息获取
- `src/trayicon.cpp`：系统托盘管理
- `src/config.cpp`：配置管理
- `src/transferstation.cpp`：文件中转站（可选）

### 开发命令
- **构建**：`cmake --build build`
- **清理**：`cmake --build build --target clean`

## 贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add some amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 打开 Pull Request

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

## 致谢

- **ImGui**：Dear ImGui 库提供了优秀的即时模式 GUI
- **DirectX**：微软的图形 API
- **苹果灵动岛**：灵感来源


---

*如果您喜欢这个项目，请给它一个星标 ⭐ 支持一下！*