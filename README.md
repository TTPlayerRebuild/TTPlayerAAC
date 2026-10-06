# ttp_aac

TTPlayer 重建版的独立 AAC／MP4 插件工程。
DLL 文件／产品版本与发行包统一采用 Actions 的北京时间日期及同日补丁编号。
插件源码、构建脚本和发行包位于本工程；FAAD2 解码核心在构建时下载固定版本，运行时不加载原 `ttp_aac.dll`。

## 功能

- 恢复原插件的 MP4 Reader、AAC Reader、AAC Decoder、Nero HE-AAC Encoder 四类接口。
- AAC ADTS、普通 MP4/M4A，以及本地分片 MP4 的解码、时长和定位。
- 普通 MP4 的标签与 JPEG/PNG 封面读取、写入、删除；AAC 标签调用宿主 `CreateStdContent`。
- Nero 编码、标签传递和配置入口；编码仍需原本独立的 `Aac.dll`、`aacenc32.dll`、`NeroIPP.dll`。
- 一个 x86 DLL 同时供普通版和 XP SP3／Win7 版使用；CPU 需要 SSE2。

分片 MP4 的标签／封面目前只读。在线 DASH、DRM、不可定位的网络流不在本版支持范围。
ADIF、AAC Main/SSR/LD 等缺少完整实测样本，不能据此声称与原 DLL 所有分支完全一致。
恢复依据、验证记录及具体限制见 [RECONSTRUCTION.md](docs/RECONSTRUCTION.md)。
全部 AAC 分析、构建对比与兼容性记录见 [文档目录](docs/README.md)。

## 构建

需要 Visual Studio C++ x86 工具、Windows SDK、CMake、Python 3。
默认使用 VS 2026（要求 CMake 4.2+）；也可传 `-Generator 'Visual Studio 17 2022'`。
首次配置下载带 SHA-256 校验的 FAAD2 **2.11.3**、VC-LTL 5.3.1 与 YY-Thunks 1.2.2。
FAAD2 源码不提交到 Git；仓库保留完整许可、作者信息、来源记录及构建适配脚本。
采用现代 MSVC + VC-LTL + YY-Thunks；不需要 VS2019 Build Tools 或 `v141_xp`。
默认按体积优化：C++17、`/O1 /Ob2 /Os /GL`、链接时优化和未使用代码移除，
保留 `/fp:precise`、异常处理及 SSE2。VS2026 实测 DLL 为 **361 KiB**。

```powershell
./build.ps1 -Package
```

输出在 `build/Release`：

- `ttp_aac.dll`：实际插件。
- `ttp_aac-yyyy.MM.dd.zip`：仅 `AddIn/ttp_aac.dll` 和 `SHA256SUMS.txt`；包内校验 DLL。
- `SHA256SUMS.txt`：ZIP 的校验值。
- `legacy-imports.json`：留在本地构建目录的 XP／Win7 导入检查报告，不加入 ZIP。

发布时使用 `./build.ps1 -Package -SourcePackage`，另生成 `ttp_aac-yyyy.MM.dd-source.zip`。
该源码包包含本工程与实际使用的 FAAD2 解码源码，可独立构建，不需要再次下载 FAAD2；
VC-LTL、YY-Thunks 仍按固定版本获取。运行 ZIP 的内容不变。

可用 `-PackageVersion '2026.09.22p1'` 同时指定 DLL 文件／产品版本与包名版本；
本地仓库构建默认使用北京时间日期。Windows 字符串版本为 `2026.09.22p1`，
四段数字版本为 `2026.9.22.1`；无 `pN` 时最后一段为 `0`。
源码发行包中的 `BUILD_VERSION` 保存原发行版本，日后重建默认沿用，可用上述参数覆盖。
具体规则见 [日期构建版本](docs/BUILD_VERSION.md)。

已有依赖缓存可通过 `-CMakeArguments` 传入
`-DFETCHCONTENT_SOURCE_DIR_TTPLAYER_YY_THUNKS=...` 和
`-DFETCHCONTENT_SOURCE_DIR_TTPLAYER_VC_LTL=...`，或通过
`-DFETCHCONTENT_SOURCE_DIR_TTPLAYER_FAAD2=...` 指向已解压的原版 2.11.3 源码。
兼容适配只应用于构建目录内的副本，不修改下载缓存。

也可直接运行 CMake：

```powershell
cmake -S . -B build -G 'Visual Studio 18 2026' -A Win32
cmake --build build --config Release --target ttp_aac --parallel 4
```

直接使用 CMake 时，`-DTTP_AAC_BUILD_VERSION=2026.09.22p1` 可固定版本；
设为 `-DTTP_AAC_BUILD_VERSION=` 可清除缓存中的固定值，恢复每次构建按北京时间取日期
（源码发行包优先使用 `BUILD_VERSION`）。

## 安装与重建版接入

关闭播放器，将二进制包解压至播放器目录，覆盖 `AddIn/ttp_aac.dll`，保留原来的其他插件。
新插件自行解码 AAC，无需 Media Foundation；Nero 编码组件不随本工程发行。

`ttp_aac` 与 `rebuild` 分别配置、构建、打包及发布，彼此没有 CMake 构建依赖。
rebuild 不会触发插件编译或自动复制本工程产物；安装新插件时单独解压插件 ZIP。
插件的 XP 运行库设置只用于本工程。

## 测试与 Actions

所有本地测试代码、样本、伪代码和原 DLL 对照材料只保存在 `../rebuild/tests/aac_rebuild`
与 `../rebuild/tests/media_analysis`，不包含于本工程或发行包。
Actions 手动运行时始终构建、检查系统导入和打包，不运行测试。
勾选 **Release a Version (GitHub)** 才会发布 GitHub Release：

- 版本与 rebuild 一致：北京时间 `yyyy.MM.dd`；同日已有标签或 Release 时使用 `p1`、`p2` 等数字递增后缀。
- 查询全部分页标签和 Release（包含草稿占用），按数值选择下一个编号；发布任务串行防止重复分配。
- 最终版本在编译前确定，并用于 DLL 属性、运行／源码 ZIP 和 Release 标签；发布阶段不再重命名 ZIP。
- 发布附件为运行 ZIP、独立的 `ttp_aac-版本号-source.zip` 和两份 ZIP 的 `SHA256SUMS.txt`。
- 运行 ZIP 只有 DLL 与校验文件；源码 ZIP 提供 FAAD2 对应源码、适配脚本及许可证，不包含本地测试、样本或构建产物。
- Release 说明链接到本次构建提交的源码、许可证和构建说明；不覆盖已有 Release。

## 来源与许可

插件接口、文件处理和 Nero 适配根据原插件伪代码、机器码和运行对照重建。
AAC 核心采用 FAAD2 **2.11.3**，保留原作者声明与 GPL-2.0-or-later 许可证；修改见
[ORIGIN.txt](third_party/faad2/ORIGIN.txt)。Code from FAAD2 is copyright (c) Nero AG, www.nero.com

仓库根目录的 [MIT 许可证](LICENSE) 用于项目自有代码，不覆盖 FAAD2、VC-LTL、YY-Thunks。
包含 FAAD2 的 DLL 整体分发须遵循其 GPL 条款，并提供对应源码；仅保留一行版权声明不够。
详见 [第三方许可与源码分发](docs/LICENSING.md)。

仓库保留必要的完整许可文本和来源记录；FAAD2 源码仅在构建目录及独立发行源码包中。
FAAD2 许可证位于 `third_party/faad2/COPYING`，VC-LTL 和 YY-Thunks 的许可证位于
`docs/licenses`。发行 ZIP 只携带运行文件和校验值。

## 日期版本与 Release 体积优先构建

DLL 的文件版本和产品版本使用北京时间 `yyyy.MM.dd`，同日发布补丁使用 `pN`；
例如 `2026.10.06p1` 对应固定数字版本 `2026.10.6.1`。Actions 在编译前确定最终版本，
DLL、发行包和发布标签使用同一版本。各项目继续独立构建。

Release 的统一配置见 [cmake/size_release.cmake](cmake/size_release.cmake)：
`/O1 /Os /Gy /Gw /GF`、跨模块优化和链接去除未引用代码／折叠相同代码，关闭 Release 调试信息。
本项目经 `/Ob0`、`/Ob1`、`/Ob2` 对比，默认选择 `/Ob1`；
可用 `-DTTP_SIZE_INLINE_LEVEL=0|1|2` 重新测量不同内联策略。
保留正常浮点语义、异常处理及 VC-LTL／YY-Thunks 的 XP／Win7 兼容配置。
Actions 不编译、不运行测试；本次新增的测试仅位于本地 `rebuild/tests/dll_size_versions`，不进入发行包。
