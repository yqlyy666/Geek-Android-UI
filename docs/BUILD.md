# 构建与验证

本仓库保存的是 Geek UI 修改模块及配套工具。它不包含 Geek 原程序、卸载引擎源码或已修改 EXE。

目前有证据支持的构建入口是：在指定的无闪屏基线上，重新编译并合入最新的滚动与表头修复模块。**这不是从官网原版生成全部美化效果的一键构建。** 具体限制见 [SOURCE_STATUS.md](SOURCE_STATUS.md)。

## 环境

- Windows x64。
- Python 3.10 或更新版本。
- Zig 0.13.0。该版本是本项目实际使用的版本；其他版本未验证。
- 构建 Python 依赖：`pefile`。
- 可选验证依赖：`unicorn`、`numpy`、`Pillow`、`opencv-python`。

在仓库根目录安装依赖：

```powershell
python -m pip install -r requirements.txt
# 需要运行机器码或像素验证时再安装：
python -m pip install -r requirements-test.txt
```

自行安装 Zig，将 `zig.exe` 所在目录加入 PATH；也可以在下面的编译命令中添加 `-ZigPath 'D:/Tools/zig/zig.exe'`。仓库不携带编译器或 Python 库副本。

## 重建最新滚动与表头修复模块

先准备以下本地输入：

| 输入 | 要求 |
|---|---|
| `native/geek-ui-theme-noflash-v1.exe` | 必须是本项目之前生成的无闪屏基线，SHA-256 为 `3e2de8f854a939b2862e0426560cb440872304619f904fdbedcb0314820eee7c`，大小为 4,544,000 字节 |
| `native/geek-ui-theme-noflash-v1.build.json` | 仓库已提供只含符号地址的清理版 manifest，用于继承已有主题回调地址 |

**该输入不是官网原版。** 如果手头没有上述精确基线，不能用另一版本的 Geek EXE 替代，也不要跳过脚本里的哈希和地址校验。仓库没有提供该基线的下载文件。

在仓库根目录确认基线：

```powershell
Get-FileHash ./native/geek-ui-theme-noflash-v1.exe -Algorithm SHA256
```

进入 `native/`，编译并合入最新源码：

```powershell
Set-Location ./native

./compile-helper.ps1 `
  -Source ./native-smooth-scroll.c `
  -Output ./native-header-stable-helper.exe `
  -Entry native_probe `
  -ImageBase 0x140462000

python ./build_header_stable.py `
  ./geek-ui-theme-noflash-v1.exe `
  ./native-header-stable-helper.exe `
  ./geek-ui-header-stable-v3.exe
```

脚本把新的 helper 嵌入原有 `.uianim` 节，并重定向两个列表初始化回调及两个行绘制回调。它会校验基线、PE 校验和、异常展开信息、合并后的代码映射及已有主题、排序和列宽钩子。输出是新的候选 EXE 和对应 `.build.json`；输入不会被覆盖，构建步骤也不会启动程序。

项目最后安装并验证的版本 SHA-256 为 `54dba73f8a943e5bdb55c8d9352ac675d8b599ab15cd2e4f582689c1f4ac75b7`，大小为 4,560,384 字节。源码整理后，已使用包内编译脚本及最新滚动源码、Zig 0.13.0 和指定 noflash 基线重新构建，输出 SHA-256 与此版本完全一致。构建校验产物留在源码目录之外；不同工具版本仍需自行核对结果。

## 可选验证

下面命令仍在 `native/` 目录执行。机器码检查不会打开 Geek：

```powershell
python ./verify_sort_from_top.py ./geek-ui-header-stable-v3.exe
python ./verify_scroll_render_compiled.py ./geek-ui-header-stable-v3.exe
python ./verify_header_stable_compiled.py `
  ./geek-ui-header-stable-v3.exe ./native-header-stable-helper.exe
```

最后一项同时读取编译生成的 `.obj`、候选 `.build.json` 及原始 noflash 基线，检查嵌入 helper、region 恢复、回退分支和保留的旧钩子。

运行实时窗口检查前，先生成权限降低且单实例名称隔离的测试副本。输出文件名必须是全新文件：

```powershell
python ./make_low_privilege_ui_test.py `
  ./geek-ui-header-stable-v3.exe ./geek-ui-header-stable-v3-test.exe `
  --isolate-instance

python ./verify_header_scroll_pixels.py ./geek-ui-header-stable-v3-test.exe `
  --output ./header-check-light.json --theme light --cases 12 --assert-stable
python ./verify_header_scroll_pixels.py ./geek-ui-header-stable-v3-test.exe `
  --output ./header-check-dark.json --theme dark --cases 12 --assert-stable

python ./verify_scroll_pixels_live.py ./geek-ui-header-stable-v3-test.exe `
  --output ./text-check-light.json --theme light --assert-monotonic
python ./verify_scroll_pixels_live.py ./geek-ui-header-stable-v3-test.exe `
  --output ./text-check-dark.json --theme dark --notches 4 --spacing-ms 35 `
  --assert-monotonic

python ./verify_smooth_scroll_live.py ./geek-ui-header-stable-v3-test.exe `
  --output ./scroll-regression.json --theme light `
  --state-address 0x140412C18 --state-pointer --state-select-list `
  --state-size 736 --state-fields ./smooth-scroll-state-fields.json `
  --visual-position-field presented --frame-field frames `
  --cache-opacity --modifier-cancel --redraw-cancel --region-restored

python ./verify_column_layout_live.py ./geek-ui-header-stable-v3-test.exe `
  --output ./column-check.json
```

实时检查会打开并在结束后关闭它自己启动的隔离进程，使用目标进程窗口消息和窗口 DC 检查画面，不需要全局鼠标输入。请串行运行，避免同一测试副本互相占用单实例名称。测试报告可能包含本机软件列表和绝对路径，应留在本地。

`verify_native_ui.py` 还提供旧 UI 快照的公用机器码测试框架。它的完整 UI 测试入口依赖对应的历史符号表，不应直接视作最新模块的全量测试命令。

## 历史构建工具

以下工具作为实现资料保留在同一 `native/` 目录，以保持 Python 模块和相邻文件引用：

| 工具 | 功能与限制 |
|---|---|
| `merge_helper.py` | 通用导入为空的 x64 helper 合并工具，也被最新构建脚本导入 |
| `build_candidate.py` | 基础 UI 的指针和调用点修改；需要已准备的合并镜像与符号表 |
| `transplant_verified_sort.py` | 从精确 donor 移植排序回顶 wrapper 到精确 v38 基线 |
| `build_context_slide.py` | 给已修复排序的基线加入菜单动画 |
| `install_column_wrapper.py` | 给菜单动画基线加入列边界 helper |
| `build_theme_reveal.py` | 给列边界基线加入第一阶段主题动画 |
| `build_smooth_scroll.py` | 给主题动画基线加入第一阶段平滑滚动 |
| `build_theme_noflash.py` | 给 v6 平滑滚动基线更新主题切换顺序 |

这些工具要求各自写死校验的中间基线。当前同名 C 源文件已被后续实现覆盖，不能按这张表依次运行就得到所有历史文件。历史输入哈希及缺失项见 [SOURCE_STATUS.md](SOURCE_STATUS.md)。
