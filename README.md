# Geek Android UI

[简体中文](#简体中文) | [English](#english)

## 简体中文

Geek Uninstaller 的界面体验调整与维护记录。项目由个人独立维护，与 Geek Uninstaller 官方无关联。

**UI 美化 by 一千凌一夜**

本项目围绕界面呈现、主题适配和操作连贯性进行调整，包含圆角布局、明暗主题、菜单过渡、列表滚动和辅助窗口等方面。实现基于原生绘制与交互扩展，并提供版本适配、合并和验证工具。

### 调整内容

- 圆角视觉布局、紧凑应用列表、搜索框和列宽边界调整。
- 浅色与深色主题样式，以及从点击位置扩散的主题切换效果。
- 右键菜单和顶部菜单样式与展开过渡。
- 列表滚动、绘制稳定性、排序恢复和表头显示体验调整。
- “关于”、扫描和残留发现等辅助窗口的界面调整。

### 仓库内容

- `native/`：界面扩展模块、适配与合并工具、验证脚本。
- `docs/`：构建流程、源码对应关系和验证范围。
- `CHANGELOG.md`、`NOTICE.md`：修改记录、项目归属与许可说明。

### 构建与验证

当前可复现的构建阶段基于指定的 Geek 中间基线及对应模块。输入文件需满足脚本中的 SHA-256 校验条件。环境要求和具体步骤请查看[构建说明](docs/BUILD.md)，源码对应关系见[源码状态](docs/SOURCE_STATUS.md)。

已对指定修改版本的明暗主题、滚动、排序、列宽边界和相关界面进行验证。测试方法与适用范围见[验证说明](docs/VERIFICATION.md)；其他 Geek 版本和 Windows 环境可能需要另行验证。

### 注意事项

- 本修改版基于 Geek 原版调整，与原版不能同时运行。启动修改版前，请先完全退出原版 Geek；需要切回原版时，也请先关闭修改版。
- 若仓库的 Releases 页面提供了可执行文件，请以对应版本的发布说明为准。

### 问题反馈

遇到问题请在 [GitHub Issues](https://github.com/yqlyy666/Geek-Android-UI/issues) 提交。建议附上 Windows 版本、Geek 版本、明暗主题、显示缩放比例和清晰的复现步骤。上传截图前，请先遮挡不希望公开的个人信息。

### 项目说明

Geek Uninstaller 原项目：[geekuninstaller.com](https://geekuninstaller.com/)。本项目是独立维护的第三方界面定制项目，不代表官方立场或授权。原程序的版权、商标和许可归各自权利人所有。仓库中的新增代码尚未单独指定开源许可证；公开源码本身不代表授予再分发或改作权限。更多信息见 [NOTICE.md](NOTICE.md)。

---

## English

Interface refinements and maintenance notes for Geek Uninstaller. This is an independently maintained community project and is not affiliated with the official Geek Uninstaller project.

**UI styling by 一千凌一夜**

The project focuses on visual presentation, theme support, and interaction flow. It includes rounded layouts, light and dark themes, menu transitions, list scrolling, and secondary windows. Native drawing and interaction extensions are accompanied by version adaptation, merge, and verification tools.

### Refinements

- Rounded layouts, compact application rows, search styling, and column boundary adjustments.
- Light and dark themes, including a click-origin theme transition.
- Updated context and top-bar menus with opening transitions.
- Improvements to list scrolling, rendering stability, sorting recovery, and header display.
- Interface refinements for the About, scanning, and leftover-discovery windows.

### Repository contents

- `native/`: UI extension modules, adaptation and merge tools, and verification scripts.
- `docs/`: build workflow, source mapping, and verification scope.
- `CHANGELOG.md` and `NOTICE.md`: change history, attribution, and licensing notes.

### Build and verification

The currently reproducible build stage uses a specified intermediate Geek baseline with its corresponding modules. Input files must meet the SHA-256 checks required by the scripts. See the [build guide](docs/BUILD.md) for environment requirements and steps, and [source status](docs/SOURCE_STATUS.md) for source mapping.

The specified modified build has been checked across light and dark themes, scrolling, sorting, column boundaries, and related interfaces. See [verification](docs/VERIFICATION.md) for methods and scope. Other Geek versions and Windows environments may require separate testing.

### Notes

- This modified build is based on the original Geek application and cannot run at the same time as the original. Fully exit the original Geek before starting this build; close the modified build before switching back.
- If an executable is available on the repository's Releases page, refer to the notes for that specific release.

### Feedback

Please report problems through [GitHub Issues](https://github.com/yqlyy666/Geek-Android-UI/issues). Include your Windows and Geek versions, theme, display scaling, and clear reproduction steps. Remove personal information from screenshots before uploading them.

### Project scope

Original Geek Uninstaller project: [geekuninstaller.com](https://geekuninstaller.com/). This is an independently maintained third-party UI customization and does not represent official endorsement or authorization. Copyrights, trademarks, and licenses for the original program belong to their respective owners. No separate open-source license has been selected for the added code; making source available does not by itself grant redistribution or modification rights. See [NOTICE.md](NOTICE.md) for details.
