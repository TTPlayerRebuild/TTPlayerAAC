# AAC 文档目录

## 使用与实现

- [安装说明](INSTALL.md)
- [独立构建与实现状态](AAC_PLUGIN_IMPLEMENTATION.md)
- [DLL 与 Actions 日期构建版本](BUILD_VERSION.md)
- [插件恢复记录、接口映射与支持范围](RECONSTRUCTION.md)

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

- [许可范围与对应源码分发](LICENSING.md)
- [项目许可证](../LICENSE)
- [FAAD2 许可证](../third_party/faad2/COPYING)与[来源及修改说明](../third_party/faad2/ORIGIN.txt)
- [VC-LTL 许可证](licenses/VC-LTL-LICENSE.txt)
- [YY-Thunks 许可证](licenses/YY-Thunks-LICENSE.txt)
