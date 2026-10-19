# AAC 独立项目接入

日期：2026-09-22；2026-10-19 更新见 [保存与 FAAD2 2.11.4 验证](VALIDATION_20261019.md)。

插件源码、构建及 Actions 位于本 `ttp_aac` 独立工程中；当前 FAAD2 2.11.4 在构建时下载并校验 SHA-256。
该项目不加载原 `ttp_aac.dll`；Nero 编码仍调用原本独立的三个 Nero 组件。

## 已完成

- 恢复四类 creator、AAC／MP4 读取、AAC 解码、定位、标签和封面接口、Nero 编码适配。
- 新增本地分片 MP4 索引，修复《玫瑰花的葬礼 JPN.Ver》音频 MP4 打开后立即 EOF 的问题。
- 原问题文件现可直接读取，255,302 ms，完整 PCM 90,062,848 字节；与普通 MP4 对照逐字节一致。
- AAC-LC、HE-AAC、HE-AAC v2、多采样率、六声道完成原 DLL 对照；XP SP3 和 Win7 已完成实际解码、定位、标签／PNG 封面和 Nero 编码验证。
- 修正较小输出缓冲的 PCM 分段读取，以及 Nero ctts 首包偏移对应的时长／定位规则。

## 构建与分发

在独立工程运行 `./build.ps1 -Package`，生成 `build/Release/ttp_aac.dll`、
`ttp_aac-yyyy.MM.dd.zip` 与 ZIP 校验文件 `SHA256SUMS.txt`。
ZIP 内仅有 `AddIn/ttp_aac.dll` 和 DLL 校验文件 `SHA256SUMS.txt`。
许可证和适配脚本保留在仓库，FAAD2 源码不提交 Git；发布时加 `-SourcePackage`，另提供包含对应解码源码的源码 ZIP。
同一个 x86 DLL 用于普通版和 XP／Win7 版；CPU 需要 SSE2。
正式采用现代 MSVC + VC-LTL + YY-Thunks 体积优化构建，FAAD2 2.11.3 的 DLL 为 361 KiB。
新版核心的 PCM 验证基线与原版不同，见 [升级评估](FAAD2_UPGRADE_ASSESSMENT.md)。
已卸载比较时使用的 VS2019 Build Tools 和 v141_xp。

按用户要求，播放器和插件完全分开构建：

- 移除 `rebuild/cmake/aac_plugin.cmake` 及 CMake 引用，不再添加 ExternalProject 或插件复制步骤。
- 移除 `TTPLAYER_BUILD_AAC_PLUGIN`、`TTPLAYER_AAC_PROJECT_DIR` 选项；两个工程可以独立放置。
- rebuild Actions 构建播放器；ttp_aac Actions 构建插件，各自产生独立 ZIP。
- 插件 Actions 勾选 `Release a Version (GitHub)` 时，采用与 rebuild 相同的北京时间日期／同日 `pN` 编号发布。
- 本地插件发行包按指定交付目录另存到 `rebuild/build/Release`，这不是构建依赖或自动集成。
- 没有执行远程发布。测试代码仍只在本地 `rebuild/tests`，Actions 不运行测试。

## 明确限制

分片 MP4 标签／封面只读；已支持不可定位 ADTS 流，任意网络 MP4、在线 DASH、DRM 不支持。
ADIF、AAC Main/SSR/LD、MP3-in-MP4 等尚未完成代表性样本的逐项对照。
不能称为所有原版分支已完全恢复。

XP 虚拟机恢复后，XP SP3 x86 实测已通过；同一 DLL 在原版 5.7.9 和重建版中均能播放问题分片文件。
Win7 SP1 x64 虚拟机的 x86 插件实际验证此前也已通过。详见 [XP 验证记录](AAC_XP_VALIDATION.md)。

完整地址映射、对照数据、来源与许可见 [插件恢复记录](RECONSTRUCTION.md)，
测试日志在本地 `rebuild/out/test-artifacts/aac_rebuild`。
