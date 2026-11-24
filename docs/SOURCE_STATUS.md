# 源码范围与可复现状态

## 仓库包含什么

这是 Geek Uninstaller 的非官方原生 UI 修改模块源码整理。新增 C 代码通过原进程已有导入表和固定调用地址工作，Python 工具负责 PE 合并及钩子修改。

本仓库没有 Geek 原程序、卸载引擎源码、原版安装包、完整修改版 EXE 或编译器。保存的源码不应被理解为 Geek 官方源码，也不能独立编译成完整卸载工具。

## 最新实现模块

| 文件 | 内容 |
|---|---|
| `native-smooth-scroll.c` | 最新平滑滚动、单一呈现、原生位置提交、正文 region 保护及表头闪烁修复 |
| `native-scroll-render.h` | 字体与列矩形缓存，供滚动模块包含 |
| `native-theme-reveal.c` | 点击位置开始的圆形主题扩散、切换前遮挡和新主题准备顺序 |
| `native-column-boundaries.c` | 固定左右外边界、内部列宽可调、列顺序锁定 |
| `native-context-slide.c` | 原生右键和顶部下拉菜单向下弹出动画 |

上述独立模块与最近测试版本的功能相对应。最新表头模块具有单独的构建脚本 `build_header_stable.py`；其余模块仍依赖历史基线。

## 基础 UI 源码快照

以下文件保留界面绘制、窗口、搜索、菜单及对话框实现，供理解和后续维护：

- `native-ui.c`
- `native-ui-surface-sort-from-top.c`
- `native-about.h`、`native-chrome.h`
- `native-menu-opaque.h`、`native-menubar-items.h`
- `native-popup-items.h`、`native-popup-panel.h`、`native-popup-separator.h`
- `native-scan-dialog.h`
- `native-search-style.h`、`native-surface.h`
- `native-search-style-sort-from-top.h`、`native-surface-sort-from-top.h`
- `native-status-layout.h`、`native-status-style.h`
- `native-tooltip.h`

`native-ui.c` 在修复排序问题期间加入过多轮恢复处理；它没有冻结为最终安装版本所继承的 v38 主 UI 源码快照，不能保证重新编译整个文件便会得到相同的基础 UI 二进制。

`native-ui-surface-sort-from-top.c` 保存已采用的 `ui_sort_from_top` wrapper 的来源。最终构建通过 `transplant_verified_sort.py` 从精确 donor 复制了这个编译函数，保留旧基线的其余界面代码；没有把该文件的整个 UI 实现替换进去。

## 为什么尚未从官网原版一键重建

项目开发时在已验证中间 EXE 上逐步合入新模块。现有脚本保护性地固定了输入 SHA-256、函数地址和布局；同名 C 文件则经过后续迭代。因此只收集当前源码，并不能补齐每一阶段的冻结输入。

当前记录的依赖链如下：

| 阶段 | 脚本实际要求的输入 SHA-256 |
|---|---|
| 排序 wrapper 移植到 v38 | 基线 `be59448ef76c66396b27b2058532d560a82af84ad0e5fe39e02529f6671d0747`；donor `7cf70b1f5c17fc9fa227e146c2d5f1358d186c5d40ff83913e187257193ce192` |
| 菜单动画 | `37e9a13065eb74b9ef42eb47dbb38964271e6bee2c5d88f85eab803b219d63ea` |
| 列边界 | `fc14fd5af1abfdb021420435f162961f817ee98caf32d1b25bad741161886d5e` |
| 主题扩散 | `8b087eb271c62b7fabc6623fab346d6a52eb549c7c1ca678bfd14ec21f43ef8a` |
| 平滑滚动 | `cda17634782eac11a7866387fce907b96ae92acff9950e595b9da32eadc525fa` |
| 主题无闪屏 | `abc7b38919289f3716ca7ec7dfce935329ca5be2704b6b6ce02f44d0582a0a20` |
| 最新表头修复 | `3e2de8f854a939b2862e0426560cb440872304619f904fdbedcb0314820eee7c` |

缺失的可复现信息包括：v38 基础 UI 的精确冻结源码、从原始资源和原厂 x64 文件生成该基线的完整步骤，以及部分阶段迭代时的冻结源代码和编译参数。项目不承诺当前工具能从任意 Geek 版本生成完整成品。

可以单独重建的范围是：使用正确的 noflash 基线和 manifest，重新编译最新 `native-smooth-scroll.c`，通过 `build_header_stable.py` 合入最新滚动及表头修复。操作方法见 [BUILD.md](BUILD.md)。

## 发布目录整理

`native/` 保持平铺，便于 `#include` 和 Python 相邻导入：共 22 个 C/H 文件、10 个构建工具、9 个 Python 验证或测试副本工具、1 个滚动状态字段定义及 1 个只含符号地址的基线 manifest。

发布副本已删除对开发机 `../tooling` 的 Python 搜索路径注入，改用 requirements 安装库；编译脚本支持 PATH 中的 Zig 或显式 `-ZigPath`。原有工作文件保持原样。

此目录不包含历史 EXE、编译中间文件、测试输出、截图、安装软件清单或本机绝对路径。可公开代码的许可和原程序归属应分别说明；源码整理本身不表示取得原程序二次分发授权。
