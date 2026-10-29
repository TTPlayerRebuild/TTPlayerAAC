# AAC Main 耦合通道与 SSR 剩余实现分析

日期：2026-10-29。依据固定的 FAAD2 **2.11.4**、原 AAC 插件伪代码／公开工厂调用及本地样本验证。本轮只调查并运行私有负对照，**没有开启生产 `COUPLING_DEC`／`SSR_DEC`，没有更换解码后端，也没有修改原 DLL 或用户歌曲**。

## 结论

这两项均不能通过开启一个宏完成：

| 项目 | 已有实现 | 实际缺口 | 本轮判断 |
|---|---|---|---|
| Main 等 AAC 类型使用的 CCE 耦合通道 | 受控返回 FAAD error 6；关闭的分支只能跳过部分语法 | 保存目标与增益、重建耦合信号、在正确位置合成、状态与边界验证 | 可在现有 ABI 和 XP／Win7 条件下实现，但需要改造帧内处理结构，不属于单点补丁 |
| SSR | 部分语法、子带滤波和 IPQF 的历史代码 | 编译错误、增益曲线空实现、子带缓冲布局、状态复位及并发初始化、可靠参考样本 | 需独立完整实现与验证；当前证据不足以发布 |

Main 本身并非全部不支持：此前 Main 单声道 PCE 已通过。CCE 也不是 Main 或六声道的专属特征，因此不能靠“Main”或“六声道”标签提前一概拒绝。

## 1. 原插件的公开调用与伪代码

原 DLL SHA256：`49195708ba9b942edd8215ffcb4e5db73a1548b0c02a5e2b11960ea12af69135`。

继续采用原工厂枚举：0 为 MP4 Reader，2 为 AAC Decoder；通过公开 `Open → Format → Initialize → Read/Input/Output` 调用，没有调用二进制内部函数，也没有修改映像。

`0x60006604` 从 `WAVEFORMATEX/WAVEFORMATEXTENSIBLE` 中取得 ASC，调用核心配置，再设置 float 输出。`0x60006d71` 调用核心解码入口 `0x600073e0`，随后以输出 sample 数设置宿主缓冲长度，并返回成功；该封装没有把本轮遇到的核心耦合错误转换为失败 HRESULT。

本轮通过 REA 6.5.0／Ghidra 12.1.3 对原副本重新取得 `0x60006d71` 证据，记录为 `rea-original-output.json`；证据 ID：`ev_00930f851a32c6931268668cac5e7c27f58fa3948dd3443a03302ae6921e4dac`。伪代码属于分析结果，不是原源码。

真实 `am05_44.mp4` 调用结果：原 Reader `Open=S_FALSE`，Format 与 Decoder Initialize 成功，报告 44.1 kHz、6 声道；连续 128 次空输出后测试停止。现重建版在 Open 的解码探测时返回 `ERROR_INVALID_DATA`。这证明原公共链路本身也没有给该测试输出有效音频，不能以它反复返回成功作为正确性目标；也不能把这次有界观察扩大为所有原宿主路径的结论。

## 2. 耦合通道为什么需要帧内处理改造

固定源码的 `syntax.c::coupling_channel_element` 明确标注只用于跳过位流。它读取后丢弃目标元素 tag、左右声道选择、耦合域、增益符号／尺度、增益列表及量化频谱，既没有保存 CCE 状态，也没有把信号加回目标声道。[FAAD2 2.11.4 syntax.c](https://github.com/knik0/faad2/blob/2.11.4/libfaad/syntax.c)

直接启用 `COUPLING_DEC` 的私有编译失败：该关闭分支使用了未声明的 `ics`，报 C2065／C2223。只在私有副本将这个访问改为局部 `ics_empty.is_used` 后，可以运行原有的“跳过”分支，但它仍然不是解码实现。

关键结构问题：当前 SCE／CPE 在读到元素后立即调用 `reconstruct_single_channel`／`reconstruct_channel_pair`，完成预测、TNS、滤波与状态推进。如果后面的 CCE 需要在 TNS 或 IMDCT 之前影响该元素，目标频谱已经被消费。简单追加一次混音，或再次重建目标，都会造成阶段错误或重复推进预测／重叠状态。

完整实现至少需要：

1. **每帧保存元素。** 按 SCE／CPE／LFE 的类型及 `element_instance_tag` 建立有界映射，暂存 ICS、量化系数、CCE 目标与增益，不把文件内编号当数组下标。
2. **完整解码 CCE。** 保存独立／依赖模式、耦合点、CPE 的左／右选择、共享或分别的增益列表、符号／指数尺度和逐频带增益；保留耦合信号自身需要的预测、TNS、窗与历史状态。
3. **分阶段重建。** 将频谱准备、TNS、IMDCT／重叠相加拆开。在对应的 TNS 前、TNS 后且 IMDCT 前，或 IMDCT 后分别应用依赖／独立耦合。必须保证每个正常声道每帧只推进一次预测及滤波状态。
4. **正确处理时间和声道。** 耦合源不是额外播放声道；输出数量、顺序、时长及现有首帧抑制规则保持不变。独立耦合也需要自己的滤波历史，不能直接把量化系数当 PCM。
5. **验证与失败清理。** 检查引用不存在的元素、重复／循环关系、无效增益、短包和窗分组不一致；失败不得返回缺失耦合声部的“成功 PCM”。Reset／Seek、打开失败重试、两个 Reader 并存都必须独立。

可参考已实现三个耦合阶段的 FFmpeg AAC 解码源，但不能把它当作只需拷贝一个混音函数的接口替换。若采用其具体代码，必须同时处理源码授权与依赖边界；本轮未引入任何新后端。

### 已验证的“只开启宏”反例

使用 [FFmpeg FATE `am05_44.mp4`](https://fate-suite.ffmpeg.org/aac/am05_44.mp4)，ASC 为 Main、PCE 指定 6 声道，44.1 kHz。私有跳过分支前五帧均观察到独立耦合、3 个目标和 5 个增益列表。

跳过分支返回 error 0，完整产生 132,096 帧有限 float PCM。与 FFmpeg 的 133,120 帧输出去掉原有首 1,024 帧后比较：

| 对比 | 结果 |
|---|---:|
| 最大绝对误差 | 0.2978254989 |
| 总 RMS 误差 | 0.0458110794 |
| 五个非 LFE 声道 RMS 误差 | 0.0466～0.0519 |
| LFE RMS 误差 | 约 1.11×10⁻⁸ |

这不是浮点舍入差：未受耦合影响的 LFE 基本一致，其余声道缺少耦合信号。该私有程序只是失败方案的负对照，不能分发为插件。

## 3. SSR 的缺口比宏开关更深

`common.h` 没有开启 `SSR_DEC`，`can_decode_ot` 因此不接受 SSR。开启宏后的私有构建仍失败：`specrec.c::allocate_channel_pair` 使用并不存在于该函数作用域的 `cpe`；其参数实际为声道编号。

即使修正这些编译问题，也没有完整算法：

- `ssr.c::ssr_gc_function` 没有展开 `alevcode/aloccode` 增益控制点，也没有完成跨帧增益函数；最终直接令所有增益为 1。[FAAD2 2.11.4 ssr.c](https://github.com/knik0/faad2/blob/2.11.4/libfaad/ssr.c)
- `ssr_decode` 向 `time_tmp + band * ssr_frame_len` 写入长度为 `2 * ssr_frame_len` 的滤波结果，而后续增益函数按 `band * frame_len * 2` 寻址。子带临时区布局必须统一，并重新验证长／短窗及切换窗，而非仅把函数接通。
- 四带逆滤波、增益补偿、重叠相加和 96-tap IPQF 必须按统一的维度与延迟定义组合；单测每个阶段后，还需要真实码流的参考 PCM。
- `ssr_ipqf.c` 用进程全局系数指针及未同步的 `initFlag` 进行首次分配。两个 Reader 同时进入时需要去除竞争，优先采用不可变系数或兼容 XP 的受控初始化，不能增加 Vista 以上 API 的硬依赖。
- 必须补齐和核对 SSR 增益历史、子带重叠缓冲、IPQF 历史在关闭／重开／定位时的处理。当前普通 AAC 的 Reset 验证不能代替 SSR 状态测试。

当前 FFmpeg AAC 源的工具列表同样没有完整 SSR gain control 与 SSR filterbank，因此不能把 FFmpeg 接受一个标为 SSR 的输入，或对改写 AOT 的 LC 数据产生输出，直接当作 SSR 音频正确性的参考。

## 4. SSR 样本目录存在误导

此前 p2 私有目录只有 SSR 目录的 HTML 索引，没有真实 SSR 编码的媒体。本轮实际下载 [FFmpeg SSR 样本目录](https://samples.ffmpeg.org/A-codecs/AAC/ssr/) 唯一的 `Dailymotion_-_Los_Sucesos_de_Bagua_-_un_v_deo_de_Creaci_n.mp4`：

- 文件 50,024,547 字节；SHA256：`4d2c90d6f814d9c6b4e2f0192dbcf7015683eb66b945bd8379b73c0a0924781c`。
- 音轨 ASC 是 `12 10`，即 **AAC-LC／44.1 kHz／立体声**，不是 AOT 3 的 SSR。
- FFprobe 报告 LC；提取前 5 秒且不转码后，原插件和重建插件均能通过公开工厂输出音频。这只验证 LC，不能算 SSR 通过。

因此 SSR 仍缺可靠正样本及独立参考 PCM。需要能够确认来源与 AOT 的一致性测试向量，覆盖增益控制非零、长／短窗及窗切换、多个子带和声道；不能简单将现有 LC 样本的 AOT 改成 3 来替代。搜索到的目录名、扩展名或成功 Open 都不足以证明编码类型正确。

## 5. ABI、兼容性与建议顺序

这些算法可以静态置于现有 FAAD 私有层，保持原 Reader／Decoder vtable 和 float PCM 输出接口不变，也不必增加外部 DLL。XP／Win7 兼容在技术上可保持，但仍需实际构建和虚拟机验证，不能把目前其它配置通过的结果直接移用。

建议先独立补齐 CCE，以可用的 Main 与 LC 耦合向量验证全部耦合阶段，再推进 SSR。CCE 验收应覆盖不同耦合点、CPE 左右选择、共享／逐频带增益、PCE 映射、定位、重开、双 Reader 和损坏输入；SSR 应先取得可靠参考样本，再修复增益函数、子带滤波与 IPQF，最后接入并发和定位链路。

可以单独考虑的诊断改善是将核心明确的 error 6 映射为“尚不支持耦合通道”，区别于损坏输入；它仍应失败，不得清除错误并输出空音频。不要用扫描字节碰到 `ID_CCE` 的模式匹配提前判断，它只能在正确的 AAC 位流语法上下文中识别。本轮没有变更生产错误映射。

## 6. 私有证据与重现

全部位于 `rebuild/tests/plugins_followup_20261029p4/aac-tools/`，不进入 Actions、发行包或仓库：

- `identities.json`：原 DLL、基线重建 DLL、真实 Main 样本及提取包的 SHA256。
- Main 样本 SHA256：`c2c82c447661b4e8fc1da278fffb19fc822e0fd8e0db912bfe4f7bfa2af280ae`。
- `source.json`、`ssr-probe.json`：目录中的疑似 SSR 文件来源、摘要及实际 LC 格式。
- `public-factory-results.json`、`original-*.pseudo.c`、`rea-original-output.json`：原公开链路与对应静态证据。
- `private-build.py`：只复制到本私有目录后编译；`coupling-plain`／`ssr-enable` 保存直接开启宏的编译失败，`coupling-skip` 只补编译访问以运行既有跳过代码。
- `main-coupling-skip.log`、`coupling-skip-comparison.json`：有效 Main 样本的错误音频负对照及逐声道误差。

本分析没有把编译失败、仅成功初始化、全零输出、仅有限数值或目录名当作功能完成；也没有新增生产依赖。
