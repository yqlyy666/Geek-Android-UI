# Geek Android UI

[简体中文](#geek-android-ui) | [English](#geek-android-ui-english)

为 Geek Uninstaller 制作的非官方安卓 / Material 风格 UI 修改模块。

**UI 美化 by 一千凌一夜**

本项目通过 C 编写原生绘制和交互钩子，再由 Python 工具合并到指定版本的 x64 EXE。界面和交互仍在 Geek 的原生进程内运行。

## 本仓库提供什么

这里提供新增 UI 模块、动画与修复代码、PE 合并工具和验证脚本。

**这不是 Geek Uninstaller 原程序的完整源码，也不是从官网原版一键生成当前成品的完整构建工程。** 最新滚动与表头修复可以基于指定的已修改中间版本重建；基础 UI 的部分文件是开发过程中保留的源码快照。具体对应关系见 [源码状态](docs/SOURCE_STATUS.md)。

## 界面与交互改动

- 安卓风圆角界面、紧凑应用卡片，以及明暗主题配色。
- 顶部圆角搜索框，统一标题栏、底部统计和控件外观。
- 右键菜单与顶部菜单的圆角样式、向下弹出动画。
- 从点击位置扩散的圆形主题切换动画。
- 平滑列表滚动、字体与绘制缓存、文字回跳修复。
- 固定程序名称的最左边界与安装时间的最右边界，保留内部列宽调整。
- “关于”、扫描与残留发现等二级窗口的样式调整。
- 滚动后排序导致列表消失、透明边缘、表头闪烁等问题的修复。

这些功能对应已测试的修改版；不能将每个源码快照单独编译后视为完整成品。

## 目录

~~~text
native/                 C / H 模块、合并工具与验证脚本
docs/BUILD.md           工具依赖、最新模块构建方法
docs/SOURCE_STATUS.md   源文件对应关系与尚未补齐的构建链
docs/VERIFICATION.md    最新修复的验证方法和范围
CHANGELOG.md            修改记录
NOTICE.md               项目归属与许可说明
requirements.txt        Python 构建依赖
requirements-test.txt   机器码及像素测试依赖
~~~

## 构建入口

需要 Windows x64、Python 3.10+ 和 Zig 0.13.0。先阅读 [构建说明](docs/BUILD.md)。

目前能重建的最新阶段是：

~~~text
指定 noflash 中间基线 + 最新滚动模块
    → 合并滚动与表头修复
    → header-stable-v3 修改版
~~~

输入基线必须匹配脚本要求的 SHA-256。仓库不附带 Geek 原程序、已修改中间 EXE 或第三方编译器。

## 当前验证状态

最新表头修复在浅色、深色模式下完成了连续滚动像素采样，24 次滚动、4,024 帧未检测到表头闪空或隐藏。另检查了文字移动、滚动后排序、列宽边界及区域恢复。详见 [验证说明](docs/VERIFICATION.md)。

这描述的是指定修改版和本次测试环境，不能替代对其他版本或 Windows 环境的测试。

## 反馈问题

请在 Issues 中提供 Windows 版本、显示缩放、明暗主题、使用的文件版本，以及从打开窗口开始的复现步骤。截图请先遮住不希望公开的个人信息。

## 原项目与许可

Geek Uninstaller 原项目：<https://geekuninstaller.com/>。

本项目为非官方 UI 修改项目。原程序的版权、商标及许可仍归各自权利人。本仓库尚未为新增代码指定开源许可证；公开源码不代表自动采用 MIT、GPL 等许可证。详见 [NOTICE.md](NOTICE.md)。

---

# Geek Android UI (English)

An unofficial Android / Material-inspired UI modification module for Geek Uninstaller.

**UI styling by 一千凌一夜**

This project implements native drawing and interaction hooks in C, then uses Python tools to merge them into a specified x64 executable. The interface and interactions continue to run inside Geek's native process.

## What this repository contains

This repository contains the added UI modules, animations and fixes, PE merge tools, and verification scripts.

**This is not the complete source code of Geek Uninstaller, nor is it a complete build project that can reproduce the finished application from the official release with one command.** The latest scrolling and header fixes can be rebuilt using a specific modified intermediate build. Some files for the base UI are source snapshots retained during development. See [Source status](docs/SOURCE_STATUS.md) for details.

## UI and interaction changes

- Android-inspired rounded UI, compact app cards, and light and dark themes.
- A rounded search box at the top, with a unified title bar, footer statistics, and control styling.
- Rounded context and top-bar menus with downward opening animations.
- A circular theme transition that expands from the click location.
- Smoother list scrolling, font and drawing caches, and a fix for text jitter.
- Fixed left and right outer column boundaries for program names and install dates, while allowing internal column widths to be adjusted.
- Restyled secondary windows, including About, scanning, and leftover discovery.
- Fixes for issues such as the app list disappearing after sorting following a scroll, transparent edges, and header flicker.

These features correspond to tested modified builds. Compiling each source snapshot on its own does not produce the complete finished application.

## Directory layout

~~~text
native/                 C / H modules, merge tools, and verification scripts
docs/BUILD.md           Tool dependencies and build steps for the latest module
docs/SOURCE_STATUS.md   Source mapping and gaps in the build chain
docs/VERIFICATION.md    Verification method and scope for the latest fixes
CHANGELOG.md            Change history
NOTICE.md               Project attribution and licensing notes
requirements.txt        Python build dependencies
requirements-test.txt   Machine-code and pixel-test dependencies
~~~

## Build entry point

A Windows x64 environment, Python 3.10+, and Zig 0.13.0 are required. Start with the [build guide](docs/BUILD.md).

The latest stage that can currently be rebuilt is:

~~~text
specified no-flash intermediate baseline + latest scrolling module
    → merge the scrolling and header fixes
    → header-stable-v3 modified build
~~~

The input baseline must match the SHA-256 required by the scripts. This repository does not include the original Geek executable, a modified intermediate executable, or a third-party compiler.

## Verification status

The latest header fix was checked in both light and dark themes with continuous scrolling pixel sampling: 24 scroll runs and 4,024 frames showed no blank or hidden header. Text movement, sorting after scrolling, column boundaries, and region recovery were also checked. See [Verification](docs/VERIFICATION.md).

These results apply to the specified modified build and the test environment used. They do not replace testing with other versions or Windows environments.

## Report an issue

When opening an issue, include your Windows version, display scaling, theme, file version, and reproduction steps starting from launching the app. Please hide any personal information you do not want to make public before attaching screenshots.

## Original project and licensing

The original Geek Uninstaller project is available at <https://geekuninstaller.com/>.

This is an unofficial UI modification project. Copyrights, trademarks, and licenses for the original program remain with their respective owners. No open-source license has been selected for the added code in this repository. Publishing source code does not automatically grant an MIT, GPL, or other license. See [NOTICE.md](NOTICE.md).
