# KeyCastOW-zh-CN

KeyCastOW-zh-CN 是 [KeyCastOW](https://github.com/brookhong/KeyCastOW) 的简体中文汉化版本，用于在 Windows 桌面上实时显示键盘按键和鼠标操作，适合录屏、演示、教学和直播场景。

本仓库在原版 KeyCastOW 的基础上进行了中文本地化和工程维护，当前产品名为 **KeyCastOW-zh-CN**。

## 项目特点

- 汉化设置界面、菜单、提示文本和版本信息
- 保留 KeyCastOW 轻量、绿色、便携的特点
- 支持键盘按键、组合键、鼠标点击、双击和滚轮显示
- 支持自定义字体、颜色、透明度、边框、圆角、显示行数和位置
- 支持快捷键开启/关闭输入捕获
- 使用 Windows 原生 API 和 GDI+ 渲染，无额外运行时依赖

## 相比原版的主要更新

1. **更新构建工具链**

   - 将工程平台工具集更新为 `v145`，适配新版 **Microsoft C++ Build Tools**。
   - 增加适用于新版 Visual Studio / VS2026 环境的解决方案文件，方便直接打开和构建。

2. **中文本地化与中文支持优化**

   - 对资源文件、设置窗口、托盘菜单、版本信息等界面内容进行了简体中文汉化。
   - 优化程序对中文显示和中文输入环境的支持。
   - 保留原有配置项和使用习惯，降低迁移成本。

3. **优化源码可读性**

   - 整理关键源码结构说明。
   - 为 `.cpp` 和 `.h` 文件补充中文注释。
   - 对显示引擎、输入捕获、设置保存、定时器、窗口过程等核心逻辑增加说明，便于后续维护和二次开发。

## 构建环境

- Windows
- Microsoft C++ Build Tools / Visual Studio（支持 `v145` 平台工具集）
- Win32 / x86 目标平台
- Unicode 字符集

## 构建命令

在 Developer Command Prompt 或已配置 MSBuild 的终端中运行：

```bat
msbuild keycastow.vcxproj /p:Platform=Win32 /p:Configuration=Release
```

如果本机存在多个工具集，也可以显式指定 `v145`：

```bat
msbuild keycastow.vcxproj /p:Platform=Win32 /p:Configuration=Release /p:PlatformToolset=v145
```

构建成功后，程序输出位于：

```text
Release\keycastow.exe
```

## 许可证

本项目沿用原版 KeyCastOW 的 MIT License。
