# MP4 多解码配置、定位与 gapless 验收（2026.10.30p1）

本轮恢复 MP4 / M4A 的多个 sample description 选择，并在不改变原版 AddIn ABI 的前提下支持实际输出格式相同的 AAC 配置切换。独立测试还发现并修复了 `iTunSMPB` 依赖不可靠采样率提示，以及 Nero HE-AAC 元数据单位不同造成的裁剪问题。

生产代码在 `src/mp4.h`、`src/mp4.cpp`、`src/reader.cpp`。没有改动原插件 DLL、原版宿主接口或者用户媒体。继续使用现有 x86 / SSE2、VC-LTL、YY-Thunks 构建，不增加运行时 DLL 依赖。

## 原有问题与恢复方式

| 问题 | 处理 |
| --- | --- |
| 固定读取 `stsd[0]`，`stsc`、`tfhd`、`trex` 的配置索引只能为 1 | 保留所有 `AudioDescription`，每个 `Sample` 记录 1-based 描述索引；按经典 / 碎片表实际选择 |
| 第一项不支持，但实际只使用第二项时无法打开 | 使用第一份实际 sample 的描述确定格式；未被引用的不支持条目可跳过 |
| `stsd` 声明数量与条目数量不一致、`stsc.first_chunk` 重复 / 倒序未严格检查 | 检查数量、索引范围、chunk 顺序和覆盖范围；错误表受控拒绝 |
| 单一 ASC 无法解码同轨道后续配置 | 使用 Reader 内部 PCM 路径，在配置边界重新配置解码器；原公开 Reader vtable 不变 |
| 同一配置的重复 entry 或不准确的 rate / channels hint 会影响解码 | AAC 以 ASC 和实际解码结果为准；提示字段不决定真实 PCM 格式 |
| 索引频率与显式频率表达同一 ASC 时可能不必要地清空历史 | 规范化此精确语法转换，保持其他配置位，等价配置不重置重叠历史 |
| 配置切换或定位导致预热块丢失、时轴缩短 | 切换后首个被 FAAD 抑制的重叠块按该 sample 的持续时间补静音；定位重放相关压缩历史 |

`tfhd` 显式描述索引覆盖 `trex` 默认值；没有显式覆盖时使用 `trex`。多个 `trun` 在同一 `traf` 中沿用该配置及正确数据偏移。保存标签保留这些描述和引用关系。

## 固定公开 PCM 格式的约束

第一次使用一种配置时，Reader 从其首个连续配置区段最多探测 64 个 packet，确认实际采样率、声道数与 Windows 声道 mask。所有使用中的配置必须得到相同的实际输出格式，之后才向原版宿主公布单一 `WAVEFORMATEX`。

真正改变配置时重建解码状态；相同 ASC 或仅频率索引写法不同的等价 ASC 延续历史。切换测试覆盖单声道 LC → SSR → LC，以及立体声 LC → HE / HEv2；后者每个 packet 的输出长度由 1024 变为 2048，输出率仍为 44100 Hz。补静音使用 sample 时长，不写死为 1024。

前进、后退、重复 edit 和跨配置定位通过重新解码恢复历史。完整播放与定位后输出的相应后缀逐字节一致。用于这种内部 PCM 路径的呈现区间必须有序且不重叠。

## iTunSMPB：提示字段与 Nero 单位

最初发现的样本：ASC 为 LC 44100 Hz 单声道，但 sample entry 的提示为 8000 Hz / 6 声道。真实解码格式可以正确识别，旧代码却在识别之前按提示字段验证 `iTunSMPB`，因此忽略有效裁剪，多输出末尾填充。HE 样本分别填写 44100、22050、8000 Hz 提示时也会出现同类差异。

修复将 `iTunSMPB` 的验证和裁剪时钟建立推迟到实际音频格式识别之后，由 `resolve_audio_clock` 处理。进一步对旧回归测试进行独立核查发现，不能把所有元数据都按实际输出 PCM 时钟解释：

| 真实 Nero 样本 | HE-AAC | HE-AAC v2 |
| --- | ---: | ---: |
| 实际输出采样率 | 44100 | 44100 |
| media timescale | 22050 | 22050 |
| delay | 2336 | 2808 |
| padding | 668 | 196 |
| content length | 44100 | 44100 |
| 三项合计 | 47104 | 47104 |
| 编码时间总量 | 46 × 1024 = 47104 | 46 × 1024 = 47104 |
| 正确输出长度 | 88200 帧 / 2 秒 | 88200 帧 / 2 秒 |

这些 Nero 元数据按 media / core 时钟计数。若一律按 44100 Hz 输出时钟处理，会错误变成 1 秒。原 `previous133` 的 `reference-3.pcm`、`reference-4.pcm` 均为 705600 字节，即 88200 帧双声道 float；没有为迁就新实现重写这些参考。

现在先以 `delay + length + padding` 对照完整编码时轴：只有 media 时钟精确匹配而 output 时钟不匹配时，选择 media 时钟；其他情况下保留 output 时钟解释，再做范围检查。此选择不读取 sample-entry hint。重复 / 多段 edit 的原有优先级保留，不让一个全文件 SMPB 覆盖其时间结构。

独立 FFmpeg 9.0.1 对两份 Nero 文件分别报告 `skip_samples=4672`、`5616`，最终均输出 88200 帧。与保留的原参考比较：HE RMS 为 `1.8388314e-6`、最大误差 `1.2443028e-4`；HEv2 RMS 为 `1.3786804e-6`、最大误差 `8.6240470e-6`。因此旧参考的时长与起点有独立依据。FFmpeg 的公开 MOV 实现也包含 [iTunSMPB 解析和 skip_samples 处理](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavformat/mov.c)。

## 独立测试材料与结果

测试保留在 `rebuild/tests/plugins_followup_20261030p1/aac-container`，不进入仓库、Actions 或源包。容器由独立 Python 构造，探针只调用公开 Reader / Decoder / MetadataCommit，不使用生产代码新增的私有结构。

- LC、48 kHz LC、立体声 LC 均由 FFmpeg 真正编码；没有修改 AOT 冒充另一种编码。关闭 PNS，使独立 PCM 比较可重复。
- SSR 使用 [ISO/IEC 14496-26 官方附件](https://standards.iso.org/iso-iec/14496/-26/ed-2/en/) 的 `as01_44` 原始 ASC、packet 与 24-bit 参考 WAV；来源和官方 MD5 校验详见 [SSR 恢复记录](SSR_IMPLEMENTATION_20261030.md)。
- HE / HEv2 使用先前已保留的真实码流；额外使用真实 Nero 原文件，改变的只有非权威 sample-entry 提示，其他编码 / 标签数据不变。
- 单配置公开解码先与独立参考验证，再作为跨配置分段精确比较基线，避免单纯对同一实现作循环比较。

| 独立单配置参考 | 比较帧数 | float RMS | 最大绝对误差 |
| --- | ---: | ---: | ---: |
| LC 对 FFmpeg，去掉编码 / 解码固定首帧差异 | 72704 | `2.6954449e-8` | `1.4901161e-7` |
| SSR 对 ISO WAV，沿用已验证的固定 1024 帧对齐 | 38912 | `3.8934127e-8` | `2.6822090e-7` |

本机最终 **65 组调用全部通过**：54 组容器 / 错误边界、7 组保存、4 组保存后重新解码。内部 PCM 正例还逐项进行 10 次定位后缀精确检查。覆盖：

1. 配置 2 起播、未使用的不支持类型 / 无效 ASC、相同 ASC 不同提示、等价 ASC 双向切换。
2. 真正 LC / SSR、LC / HE、LC / HEv2 切换，返回此前配置；编辑区段、重复与空 edit。
3. `stsc`、`trex`、`tfhd` 的有效及错误索引，多 `trun`，错误数量和顺序，重叠呈现区间。
4. 使用真实编码的 44100 → 48000 Hz、单声道 → 立体声变化，确认受控拒绝。
5. LC / HE 裁剪的错误提示；Nero HE / HEv2 各 0、22050、44100、8000 四种提示均保持原 88200 帧参考。
6. 碎片内 / 外 padding 足够及不足；多配置经典 / 碎片文件保存后配置和 PCM 不变。

验收 DLL SHA-256：`797175aedd2f538f4a6b5cc03e6e27038128fe064c67b2a462f7275f1b381369`。逐例记录为 `final/report.json`，独立 Nero 证据为 `nero-independent.json` 和 `nero-*-ffprobe.json`。

紧凑虚拟机目录 `vmstage` 使用 PCM 哈希，不复制完整参考 PCM；`run.cmd <DLL>` 共 65 调用，本机同样 `FAILURES=0`。Windows XP / Win7 结果及原版 / 重建版实际宿主验证由本轮总验收记录补充，不能以本机公开接口测试代替。

另外准备了 `host/host-switch-lc-hev2.m4a`：33.4367 秒、243240 字节、15 次真实 LC ↔ HEv2 切换。公开接口全量 PCM 与八组独立解码段精确拼接一致，定位检查通过，PCM FNV64 为 `822f61fbca92fb45`。媒体 SHA-256 为 `85ba5cff27a111caa8e449e2cbd8dc5a8d8813d4846e96936a72519732f6a86b`。

## 明确边界

- 原宿主一次公布的 PCM 格式保持不变。实际采样率、声道数或声道 mask 改变时拒绝，不在插件里偷偷重采样或重新混音。
- 一个发生变化的配置首段太短、不能产出 PCM 以证明真实布局时，保守拒绝。使用中的不支持配置、AAC / MP3 混合轨道也不自动猜测。
- ASC 等价化目前仅覆盖索引频率与相同显式频率；其他不同位模式按配置变化处理，没有声称实现所有语义等价 ASC 的规范化。
- 碎片文件保存只使用原有 metadata / padding 空间。空间不足返回 `STG_E_MEDIUMFULL`，整文件字节不变；没有搬移 `moof` / `mdat`，也没有宣称支持任意扩容保存。重复 Commit 与 Release 不重试失败保存。
- gapless 单位选择验证了 decoded-output 计数与 Nero media/core 计数两种写法。重封装后 media 时钟与原 core 时钟分离、又保留含糊 SMPB 的文件尚无完整兼容保证，不应据此宣称所有非规范标签都能准确推断。


## 三系统收尾记录

2026.10.30p1 冻结 DLL `797175aedd2f538f4a6b5cc03e6e27038128fe064c67b2a462f7275f1b381369` 已在 Win11、XP SP3、Win7 上分别通过本节 65 次公共接口调用。三系统同时通过每系统 1577 次整合回归。具体实际宿主、发行包及偶发退出观察以 `rebuild/docs/AUDIO_PLUGIN_FOLLOWUP_20261030P1.md` 的收尾记录为准。Win10 按用户约定由 Win11 代测，不声称实际运行了 Win10。
