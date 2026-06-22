# KeyCastOW-zh-CN · KeyCastOW 中文版

<p align="center">
  <img src="https://imagehost.ckh-cn.site/i/2026/06/22/uhft82.png" alt="运行效果" width="760">
</p>

<p align="center">
  <b>轻量、绿色、可高度自定义的 Windows 按键 / 鼠标操作可视化工具</b>
</p>

<p align="center">
  <a href="https://github.com/brookhong/KeyCastOW"><img alt="Based on KeyCastOW" src="https://img.shields.io/badge/Based%20on-KeyCastOW-blue"></a>
  <img alt="Windows" src="https://img.shields.io/badge/Platform-Windows-0078D4">
  <img alt="Win32" src="https://img.shields.io/badge/Target-Win32%20%2F%20x86-orange">
  <img alt="License" src="https://img.shields.io/badge/License-MIT-green">
  <img alt="Language" src="https://img.shields.io/badge/Language-%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-red">
</p>

<p align="center">
  <a href="#-为什么选择它">为什么选择</a> ·
  <a href="#-功能亮点">功能亮点</a> ·
  <a href="#-界面预览">界面预览</a> ·
  <a href="#-快速开始">快速开始</a> ·
  <a href="#-构建">构建</a>
</p>

---

## ✨ 为什么选择它

做教程、录屏、直播、课堂演示时，观众最容易错过的往往不是画面，而是你按下了什么键。

**KeyCastOW-zh-CN** 基于经典开源项目 [KeyCastOW](https://github.com/brookhong/KeyCastOW) 深度汉化与优化，让键盘输入、鼠标点击、滚轮操作以清晰、漂亮、低干扰的方式实时显示在桌面上。

- 🎬 **录屏更清楚**：快捷键、组合键、鼠标操作一眼可见
- 🧑‍🏫 **教学更顺畅**：不用反复解释“刚才按了哪个键”
- 🧰 **开箱即用**：绿色小工具，无复杂依赖
- 🎨 **样式可控**：字体、颜色、透明度、边框、圆角、位置都能调
- 🇨🇳 **中文友好**：设置界面、菜单、提示、版本信息均为简体中文

## 🚀 功能亮点

| 能力 | 说明 |
| --- | --- |
| ⌨️ 按键显示 | 实时显示键盘输入、功能键、组合键 |
| 🖱️ 鼠标显示 | 支持单击、双击、滚轮等鼠标操作提示 |
| 🎨 样式定制 | 自定义字体、字号、文字颜色、背景色、透明度、边框、圆角 |
| 📍 位置控制 | 可设置显示位置、最大行数、增长方向 |
| ⬇️ 向下增长 | 新增“向下增长”选项，适合固定在屏幕顶部或指定起点展示 |
| 🏷️ 品牌标识 | 屏幕角落品牌标识支持独立文本与样式配置 |
| ⚡ 快捷开关 | 支持快捷键快速开启 / 关闭输入捕获 |
| 🧩 原生轻量 | Windows 原生 API + GDI+ 渲染，无额外运行时依赖 |
| 💾 配置持久化 | 设置自动写入 INI 文件，重启后恢复 |

## 🖼️ 界面预览

<p align="center">
  <img src="https://imagehost.ckh-cn.site/i/2026/06/22/ufkybc.png" alt="演示" width="300">
</p>

## 📦 快速开始

1. 下载或构建 `keycastow.exe`
2. 双击运行程序
3. 在系统托盘中打开设置
4. 调整按键标签样式、显示位置、品牌标识等选项
5. 开始录屏、直播或演示

> 默认快捷键：`Alt + B` 可快速开启 / 关闭输入捕获。

## 🆚 相比原版 KeyCastOW 的主要变化

### 1. 简体中文本地化

- 汉化设置窗口、托盘菜单、提示文本和版本信息
- 优化中文显示与中文输入环境下的体验
- 保留原版使用习惯，降低迁移成本

### 2. 更适合录屏的默认样式

- 默认采用深色半透明背景、浅色文字和细朱砂色边框
- 默认字体调整为更适合中文环境的微软雅黑 UI
- 圆角、边框粗细和标签间距重新调整，观感更轻、更现代

> 新默认样式仅在首次启动、删除 INI 或恢复默认设置后生效，不会覆盖已有用户配置。

### 3. 品牌标识独立配置

品牌标识不再复用按键标签样式，可单独设置：

- 显示文本
- 字体与字号
- 背景颜色与透明度
- 文本颜色
- 背景宽高

### 4. 新增向下增长显示

勾选“向下增长”后，按键标签会从定位点向下追加，更适合把显示区域固定在屏幕上方、窗口旁边或固定起始位置的场景。

### 5. 工程维护与源码整理

- 更新工程配置，适配新版 Visual Studio / Microsoft C++ Build Tools
- 补充源码注释与结构说明
- 便于后续维护、二次开发和功能扩展

## 🛠️ 构建

### 环境要求

- Windows
- Visual Studio / Microsoft C++ Build Tools
- Win32 / x86 目标平台
- Unicode 字符集

### Release 构建

在 Developer Command Prompt 或已配置 MSBuild 的终端中运行：

```bat
msbuild keycastow.vcxproj /p:Platform=Win32 /p:Configuration=Release
```

如果本机存在多个平台工具集，也可以显式指定：

```bat
msbuild keycastow.vcxproj /p:Platform=Win32 /p:Configuration=Release /p:PlatformToolset=v145
```

构建成功后，输出文件位于：

```text
Release\keycastow.exe
```

## 📁 项目结构

```text
.
├── keycast.cpp        # 显示引擎、窗口、设置、托盘、渲染逻辑
├── keylog.cpp         # 键盘 / 鼠标低级钩子与输入捕获
├── keycastow.rc       # 简体中文资源、对话框、版本信息
├── resource.h         # 控件与资源 ID
├── timer.h            # 动画计时器封装
└── keycastow.vcxproj  # Visual Studio C++ 工程文件
```

## 🤝 致谢

本项目基于 [brookhong/KeyCastOW](https://github.com/brookhong/KeyCastOW) 进行简体中文本地化与体验优化。

感谢原作者 Brook Hong 的开源贡献。

## 📄 许可证

本项目沿用原版 KeyCastOW 的 **MIT License**。
