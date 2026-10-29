# AAC 文档目录

## 使用与实现

- [安装说明](INSTALL.md)
- [独立构建与实现状态](AAC_PLUGIN_IMPLEMENTATION.md)
- [DLL 与 Actions 日期构建版本](BUILD_VERSION.md)
- [插件恢复记录、接口映射与支持范围](RECONSTRUCTION.md)
- [2026-10-24 多段／空编辑列表及精确定位](EDIT_LIST_COMPLETION_20261024.md)
- [2026-10-25 呈现时间、分片编辑及媒体时间空隙](PRESENTATION_TIMELINE_20261025.md)
- [2026-10-29 部分读取取消、Main 容器提示字段与历史 Profile 边界](STREAM_PROFILE_BOUNDARIES_20261029P2.md)
- [2026.10.29p3 ADIF 流读取与定位后预热](ADIF_STREAMS_20261029P3.md)
- [2026.10.29p3 LD、显式频率与高采样率 SBR](LD_ASC_20261029P3.md)
- [2026.10.29p3 分片 MP4 标签／封面保存](FRAGMENT_METADATA_SAVE_20261029P3.md)
- [2026.10.29p4 原始 AAC 定位、取消恢复与 PCE](RAW_SEEK_PCE_20261029P4.md)
- [2026.10.29p4 原生 7350 Hz 与采样率边界](NATIVE_7350_RATES_20261029P4.md)
- [2026.10.29p4 Main 耦合通道与 SSR 剩余实现](MAIN_COUPLING_SSR_REMAINING_20261029P4.md)

## 原插件分析

- [Nero Aac.dll 伪代码、接口与编码流程分析](NERO_AAC_DLL_PSEUDOCODE_ANALYSIS.md)
- [原版 AAC 插件的 FAAD2 来源与 GPL 分析](ORIGINAL_AAC_GPL_ANALYSIS.md)
- [分片 MP4 无法播放的原因](FRAGMENTED_MP4_AAC_ANALYSIS.md)
- [根据伪代码与二进制重建插件的可行性](AAC_PLUGIN_REBUILD_FEASIBILITY.md)

## 兼容性与构建验证

- [原版播放器宿主兼容性](AAC_ORIGINAL_HOST_COMPATIBILITY.md)
- [XP 实测记录](AAC_XP_VALIDATION.md)
- [运行库依赖分析](AAC_RUNTIME_DEPENDENCY_ANALYSIS.md)
- [工具链体积对比与正式构建复核](AAC_TOOLCHAIN_SIZE_COMPARISON.md)
- [FAAD2 2.11.3 升级评估](FAAD2_UPGRADE_ASSESSMENT.md)

分析和实测文档保留各轮验证的时间、构建哈希及范围；正式采用的工具链与后续实测见体积对比文档。
文中的 `ttp_aac/...`、`rebuild/...` 路径均相对于本地 TTPlayer 工作区根目录。
测试代码、样本和私有分析材料仍保存在 `rebuild/tests` 与 `rebuild/out/test-artifacts`。

## 许可证

当前实现与测试：[2026-10-19 保存修正及 FAAD2 2.11.4](VALIDATION_20261019.md)。

- [许可范围与对应源码分发](LICENSING.md)
- [项目许可证](../LICENSE)
- [FAAD2 许可证](../third_party/faad2/COPYING)与[来源及修改说明](../third_party/faad2/ORIGIN.txt)
- [VC-LTL 许可证](licenses/VC-LTL-LICENSE.txt)
- [YY-Thunks 许可证](licenses/YY-Thunks-LICENSE.txt)
