# AAC 多段编辑列表补全

日期：2026-10-24。本文记录 `COMPLETION_20261024.md` 之后的多段编辑补全，最终版本为 `2026.10.24p1`；本轮验收范围与保留边界见下文，不覆盖旧发行包的摘要。

## 原因与修改

上轮 MP4 只处理一个非空、正常速率的 `elst` 项。多个项、空编辑被忽略，宿主会播放原始连续音频；整曲 `iTunSMPB` 还可能覆盖实际剪辑顺序。

本轮保持原 Reader、Decoder、Metadata 的接口和工厂编号，扩展已有 Reader 内部 float32 PCM 路径：

- 解析非碎片 AAC MP4 的 `elst` v0/v1，最多 4096 项；支持正常速率的相邻、跳跃、倒序、重复区间。
- `media_time = -1` 生成对应长度的静音，包括开头、中间、结尾和全静音轨道。零长度项跳过。
- 编辑时长使用电影时间基，媒体起点使用音轨时间基；累积电影时间后统一转换到输出样本，避免每段独立取整产生累计误差。
- 向前剪辑复用解码器状态和当前 PCM 帧；倒序、重复及反向定位重新解码前置音频，保留 AAC 的重叠状态，使结果与完整解码切片相同。
- FAAD 隐去的首个预热帧占据媒体时间；编辑明确选择该部分时输出静音，不把后续音频整体提前。
- 多段或空编辑优先于 `iTunSMPB`。只有无编辑或单段范围相符（允许一个电影时间刻度的取整误差）时，才用 SMPB 提供更精确的裁剪。
- 定位按编辑后的时间线进行；标签保存仍保留 `elst` 原始内容。
- 非法负起点、越界范围、截断、计数过大和整数溢出返回错误。变速或驻留编辑返回 `ERROR_NOT_SUPPORTED`，不悄悄播放完整原音轨。

电影时间和媒体时间的区别、空编辑以及重复片段语义以 Apple 的 [Edit list table](https://developer.apple.com/documentation/quicktime-file-format/edit_list_atom/edit_list_table) 和 [Playing with edit lists](https://developer.apple.com/documentation/quicktime-file-format/playing_with_edit_lists) 为依据。这是标准容器编辑能力的扩展，不能据此声称原插件也处理了这些分支。

## 本地验证

测试源码和所有生成音频只保存在 `rebuild/tests/plugins_followup_20261024/aac`，不加入本仓库、Action 或发行包。

| 检查 | 结果 |
| --- | --- |
| `elst` v0/v1 的 20 个合法时间线，包括静音、倒序、重复、SMPB 冲突和分段取整 | PCM 逐字节等于上一轮完整解码结果的切片拼接；每 17 ms 反向定位结果一致 |
| 9 个非法/不支持编辑 | Open 返回明确错误，无崩溃 |
| LC 单声道、6 声道、HE、HEv2、16 kHz 五种多段时间线 | 样本数、拼接 PCM 和定位逐字节通过 |
| 4 个标签提交后重新打开 | `elst` 字节未变，PCM 未变 |
| 上轮 LC、单声道、6 声道、HE、HEv2、低采样率、co64 共 7 个基线 | PCM 与上轮逐字节相同 |
| 上轮 137 样本非帧对齐的 elst v0/v1/SMPB | 三组仍通过 |
| Release x86 构建 | 通过；XP/Win7 导入检查通过，3 个系统 DLL、92 个导入 |

复现命令：

```powershell
cmake --build ttp_aac/build --config Release --target ttp_aac --parallel 4
python rebuild/tests/plugins_followup_20261024/aac/build.py
python rebuild/tests/plugins_followup_20261024/aac/build.py save
python rebuild/tests/plugins_followup_20261024/aac/edits.py
python rebuild/tests/plugins_followup_20261024/aac/profile_edits.py
python rebuild/tests/plugins_followup_20261024/aac/save_check.py
python rebuild/tests/plugins_followup_20261024/aac/regression.py
```

最终 `2026.10.24p1` DLL 已在 Windows 11、XP SP3 与 Win7 SP1 统一验收，三系统各 48 项通过。原版与重建版也在三系统播放包含回跳及中间静音的 13.9 秒样本，保存的播放时间均超过 10 秒；加载路径、无错误弹窗和自然退出均通过。

`prepare_vm.py` 还会在同一私有测试目录生成平铺的 `vmstage`、`manifest.json` 和 `run.cmd`，共 48 项；合法项使用 `fc /b` 与期望 PCM 逐字节比较，拒绝项同时检查退出码和 HRESULT。该脚本在本机运行结果为 `AAC_EDIT_CASES=48 FAILURES=0`，便于由主任务统一在 XP／Win7 上验收最终发行 DLL。

## 保留边界

以下是本轮完成时的边界；composition offset、分片编辑和媒体空隙的后续补全见 [2026-10-25 记录](PRESENTATION_TIMELINE_20261025.md)。

- 碎片 MP4 的复杂编辑、非 1 倍速/驻留播放和非连续/带非零 composition offset 的编辑媒体时间线仍未实现；新编辑路径会拒绝后者。
- 反向或重复片段为保证重叠状态一致，仍可能从文件开头解码到目标点；内存不随整曲 PCM 长度增长，但超长文件反向定位的耗时还可优化。
- AAC Main、SSR、LD 等历史 Profile 的完整实测覆盖仍未补齐。
- 本轮没有升级第三方库、增加外部解码 DLL、改动 Nero 编码依赖或扩大旧 Decoder 输出结构容量。
