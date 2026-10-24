# AAC 呈现时间与分片编辑列表补全

日期：2026-10-25。继 [上一轮多段编辑列表](EDIT_LIST_COMPLETION_20261024.md) 后，补齐本地可定位 MP4 的 `ctts`、分片 `trun` composition offset、`tfdt` 时间空隙与编辑列表组合。

## 原版证据与本轮扩展范围

沿用已经核对原始 DLL 的证据，未把新实现描述为原版已有功能：

- 原版 `600232A3` 用 `mdhd` 时长减去首个 `ctts` 偏移；`60022FB5` 在定位时加回该偏移。无编辑列表的普通 Nero MP4 继续走这条兼容路径。地址与接口对应见 [恢复记录](RECONSTRUCTION.md)。
- 原版 box 分类器 `60025141` 没有分片索引节点，reader `60005A5B` 只消费传统采样表；具体二进制与样本哈希见 [分片 MP4 原因分析](FRAGMENTED_MP4_AAC_ANALYSIS.md)。本轮分片编辑支持属于扩展能力。
- 上轮重建代码在 `fragmented` 时完全跳过 `elst`，并拒绝带编辑列表且时间不连续或 composition offset 非零的 AAC。容器能够读取这些数值，但 PCM 时间轴无法使用它们。

格式依据：

- [Apple Edit atom](https://developer.apple.com/documentation/quicktime-file-format/edit_atom)：编辑表定义媒体片段及其偏移、长度；空编辑可以延后轨道开始。
- [W3C ISO BMFF 字节流格式 §3](https://www.w3.org/TR/mse-byte-stream-format-isobmff/#initialization-segments)：初始化段的 rate=1 单条编辑可映射媒体 composition time 到呈现时间；长度为零时可以延续至后续分片。本实现将这个规则用于已经完整取得索引的本地文件，没有实现浏览器 MSE 或在线 DASH 客户端。

## 实现

### 媒体呈现位置与解码包序号分开

每包的呈现区间为 `DTS + composition offset` 到该值加采样表时长。时间轴允许负起点、正偏移和向前的空隙。两端转换到实际 PCM 采样率时都向负无穷取整，HE／HEv2 的媒体时钟与输出采样率不必相同。

Reader 按呈现区间查找对应包，再按原来的压缩包顺序维持 FAAD 重叠状态。时间空隙输出静音；不会因时间戳有空隙而跳过解码器所需的前包。反向或重复编辑重新预解码至所需包，单次只缓存一个解码包，未增加整曲 PCM 缓存或第二份采样索引。

FAAD 抑制首个重叠帧时继续沿用上轮规则：将该包在媒体时间轴上的区间保留为静音。没有猜测编码器延迟，没有删除未知数量的音频样本。

### 编辑与分片

- 普通 MP4：允许 `ctts` v0 的正偏移、v1 的正/负偏移、变化偏移形成的非重叠空隙，与多段剪切、重复、空编辑组合。
- 分片 MP4：同样应用初始化段 `elst` v0/v1，并使用 `tfdt` 和 `trun` 的实际时间。
- 分片 rate=1 单条零长度非空编辑：从指定媒体起点播放至已索引媒体末尾；尾端直接用媒体采样数，避免先换算到电影时钟造成精度损失。
- 分片未指定编辑但有偏移/空隙：按媒体呈现时间输出包含静音区间的 PCM。普通 MP4 无编辑时仍保留原 Nero 兼容路径。
- `iTunSMPB` 仅用于传统、连续且无 composition offset 的时间轴；不能覆盖多段编辑或分片时间映射。
- 时间运算检查有符号范围和溢出。重叠或倒退的呈现区间返回 `HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)`，不自行选择混音/重排策略。

### 保存与 ABI

没有改导出、接口槽位或外部运行库依赖。编辑路径继续使用原 Reader 的 PCM 输出接口。

普通 MP4 的保存保留 `elst`、`ctts` 与压缩载荷；分片文件仍报告只读，修改标签返回 `E_ACCESSDENIED`。本轮没有扩大分片写入范围。

## 核心接口验证

所有新测试、样本、日志均在私有目录 `rebuild/tests/plugins_followup_20261025/aac`。旧测试基线未改动。

| 验证组 | 数量 | 结果 |
| --- | ---: | --- |
| 普通/分片、编辑 v0/v1、正/负/变化 CTS、跨空隙、负起点裁剪、开放尾端、错误/溢出拒绝 | 40 | 通过 |
| 单声道、6 声道、HE、HEv2、16 kHz，正/负 CTS 与多段编辑 | 10 | 通过 |
| 普通文件保存保留时间表与 PCM、分片只读且文件字节不变 | 7 | 通过 |
| 上轮全部基线，包括 gapless、SMPB 冲突和异常编辑 | 48 | 通过 |
| 合计 | **105** | `AAC_TIMELINE_CASES=105 FAILURES=0` |

合法用例逐字节比较独立生成的期望 PCM 拼接；读取缓冲故意使用 65,533 字节，并每 17 ms 反向定位检查读回数据。新时间轴由已验收的同一组 AAC 包构造，期望输出由已有 PCM 基线按明确的时间区间拼接，避免仅以“可以解码”作为通过条件。

额外使用本机 FFprobe 9.0.1 解析 29 个合法时间轴，确认包数、压缩载荷及相对呈现间隔。需区分一处观察差异：FFprobe 将部分负 CTS 分片的时间原点平移至零；这些用例只将 FFprobe 用作载荷与间隔对照，绝对起点仍按构造的有符号 `trun` 字段验证，不能声称与 FFprobe 的绝对时间输出完全相同。

最终本地构建：

- 文件/产品版本：`2026.10.25`。
- 大小：397,312 字节；比上一轮 `2026.10.24p1` 增加 1,536 字节。
- SHA-256：`9ba06ade438d7c6f5347bbc8e5b4346a111189b0feaaee06fa851f04b845de16`。
- x86、子系统 5.01；XP/Win7 导入检查通过，3 个系统 DLL、92 个导入。
- 同一最终 DLL 已由主任务在 XP SP3 与 Win7 SP1 执行完整平铺探针，各 105/105 通过；加上本机 Windows 11，共三个系统各 105 项。虚拟机日志分别为私有测试目录的 `xp-result.log`、`win7-result.log`。
- 原版／重建版实际宿主已由主任务在三系统各验证普通多段编辑和新分片时间轴两类文件，合计 12 次运行通过。分片样本 20 秒、48 kHz 双声道，在 5.790～6.822750 秒有内部静音；宿主播放越过该区间，时间继续前进。逐项检查实际 DLL 路径、无错误弹窗和正常退出；这是静音输出的集成检查，不代替听音。日志见私有 `rebuild/tests/plugins_followup_20261025/integration`。

复现命令：

```powershell
cmake -S ttp_aac -B ttp_aac/build '-DTTP_AAC_BUILD_VERSION=2026.10.25'
cmake --build ttp_aac/build --config Release --target ttp_aac --parallel 4
python rebuild/tests/plugins_followup_20261025/aac/build.py
python rebuild/tests/plugins_followup_20261025/aac/build.py save
python rebuild/tests/plugins_followup_20261025/aac/timeline.py
python rebuild/tests/plugins_followup_20261025/aac/profile_offsets.py
python rebuild/tests/plugins_followup_20261025/aac/metadata.py
python rebuild/tests/plugins_followup_20261025/aac/packet_reference.py
python rebuild/tests/plugins_followup_20261025/aac/stage.py
```

`vmstage` 为平铺目录，含最终 DLL、两个 XP 兼容探针、105 项 `manifest.json` 和 `run.cmd`。运行 `run.cmd` 检查成功项的 PCM、拒绝项的 HRESULT，以及分片只读项的文件不变性。测试不进入仓库、Actions 或发行包。

## 仍保留的边界

- 呈现区间重叠/倒退、非 1 倍速和驻留编辑不支持；没有实现混音或时间伸缩。
- 分片输入必须为可定位、长度已知、索引完整的本地流；不是流式追加或网络 DASH 支持。
- 反向/重复编辑仍可能从头预解码；超长文件定位性能可继续优化。
- 未猜测 `sgpd/sbgp` 等未知预滚信息或编码器延迟；异常采样表若索取超出实际解码包的 PCM，会返回错误。
- AAC Main、SSR、LD 等历史 Profile 的完整样本覆盖与分片标签写入仍未完成。
