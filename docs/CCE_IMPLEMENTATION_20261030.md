# AAC Main／LC 耦合声道实现与验收（2026-10-30）

本文接续 `MAIN_COUPLING_SSR_REMAINING_20261029P4.md`。原版二进制、原版伪代码和公开工厂的历史证据沿用该文档；本轮没有加载原版 DLL 作为解码后端。这里实现的是实际 CCE 解码，不能用启用上游 `COUPLING_DEC` 的跳过分支替代。

## 已完成的生产改动

- 新增 `cmake/faad2_coupling_compat.cmake` 及 `src/faad_coupling/` 三个源文件。固定 FAAD2 2.11.4 下载源码不变，CMake 仅修改私有构建副本。没有新增运行时 DLL，没有修改 AddIn、Reader、Decoder 的公开 vtable 或结构。
- AAC Main／LC 的一个 raw data block 先保存 SCE／CPE／LFE 的 ICS 和量化系数，再解析全部 CCE。每个 CCE tag 拥有独立的 Main 预测状态与频谱，最后按原播放声道顺序恢复正常声道。
- 恢复 TNS 前、TNS 后／IMDCT 前、独立 IMDCT 后三个耦合位置。源频谱先完成自身 TNS；目标在指定位置叠加。独立源保留各播放声道的窗口重叠历史，逐帧修改目标路由也不会丢失历史。
- 支持 SCE 目标、CPE 的共享增益／仅右／仅左／左右分别增益，四种增益步长、公共增益及逐组逐频带差分增益、正负增益、长窗和八短窗分组、多 CCE 叠加。CCE 不计入播放声道数。
- 继续保留 `APPLY_DRC=1`。正常声道保存解析当时的逐元素 DRC 快照，避免延迟合成改变已有非 CCE 的 DRC 应用时机。独立 CCE 对相应输出声道应用同一快照及排除掩码后执行独立 IMDCT，最后应用 CCE 标量增益；源信号不会绕过 DRC。
- 拒绝重复 CCE tag、重复播放元素、缺失目标、不兼容的长／短窗、截断数据和非有限增益／输出。错误仍交给公开接口返回，不能清掉错误再输出空 PCM 伪装成功。
- Close 和 PostSeekReset 释放 CCE 私有状态。实际 core clock 透传 `ttp_core_rate`，与父任务的显式采样率支持兼容。

## Main 原有强度立体声顺序错误

上游 `reconstruct_channel_pair` 原顺序是 MS → intensity stereo → Main prediction。ISO `am05_44` 在第 91 个压缩帧进入相应强度立体声片段后，后置声道出现明显误差：后右声道 RMS 约 0.01060555，后左约 0.00078770。

正确顺序是 MS → Main prediction → intensity stereo。先恢复独立声道频谱，再由强度信息推导另一声道。本轮把原有 Main 条件块移到 intensity stereo 前；没有换用另一套预测算法。调整后同一完整样本与 ISO 24-bit WAV 的整体 RMS 为 `5.521527369557042e-7`，最大绝对误差为 `5.14984130859375e-5`。该差异有官方参考支持，属于修复原先错误波形，不应要求保留旧错误 PCM 哈希。

参考实现的顺序可在 FFmpeg `aacdec.c::decode_cpe` 中独立核查；耦合语法和三个阶段也与其 CCE 解析／合成流程交叉验证。生产代码没有链接 FFmpeg。

## 可复现的参考与样本

本机证据均放在未入库的 `rebuild/tests/plugins_followup_20261030/aac-coupling/`，原始样本及测试代码不进入发行包或 Actions。

1. ISO 官方 [14496-26 第二版附属数据](https://standards.iso.org/iso-iec/14496/-26/ed-2/en/)：从 `compressedMp4.zip`、`referencesWav.zip` 按范围读取 `am05_44` 和六路参考 WAV，逐文件校验官方 `files.md5`。Main 原 MP4 SHA256 为 `c2c82c447661b4e8fc1da278fffb19fc822e0fd8e0db912bfe4f7bfa2af280ae`。
2. FFmpeg [FATE AAC 样本集](https://fate-suite.ffmpeg.org/aac/)：`al15_44.mp4` 是六声道 LC 独立耦合，PCE 为五个前置声道加 LFE。按真实 speaker order 比较，不能把两对前置声道直接当成普通 5.1 的前／后声道。
3. Main／LC 原文件带可选 MPEG-4 DRC；FFmpeg 和官方未压缩参考的 DRC 策略不同。为隔离解码数学，`neutral_drc.py` 仅把完整 DRC fill payload 改成同长度合法 EXT_FIL 填充，保持所有编码音频元素原样。生产 DLL 仍启用 DRC。公开 Decoder 输出直接对官方金样比较，不依赖“保存本实现输出后再与自身对比”作为正确性依据。
4. Main 全长中性 DRC 版本对官方 WAV：最大误差 `5.14984130859375e-5`、RMS `5.521527369557042e-7`。LC 同样处理后对 FFmpeg：最大误差 `3.254413604736328e-5`、RMS `3.196780751175915e-7`。Main 第一帧的 1024 帧预热按公开 Decoder 的既有约定去除，未通过任意相关性搜索对齐。
5. `dependent.py` 从真实 Main 前八帧重新编码合法 dependent CCE 头和 gain 标志；TNS 前／后两个位置对 FFmpeg 最大误差分别约 `7.75e-6`／`4.93e-6`。这是公开注明的衍生向量，不冒充原始 ISO dependent 测试文件。
6. `synthetic.py` 生成 42 个有效向量，覆盖三个耦合位置、CPE 四种选择、四种 gain scale、负差分和符号、八短窗不同分组、两个 CCE、逐帧改路由。普通向量完整 PCM 对 FFmpeg最大误差不超过 `6.99e-10`。独立 CCE 的 DRC ×2 与左声道排除掩码，分别与无 DRC PCM 的明确倍数关系逐样本精确相等。

`am06_44`、`am07_44` 另作排查使用，未发现 CCE，不能因为下载了这些文件就计作额外耦合覆盖；`al17_44`、`al18_44` 也未计入 CCE 正向验收。

## 公开接口与状态验收

`public.py` 对冻结生产 DLL 验证 50 个正例，每个同时验证独立 Decoder 和 Reader → Decoder 的宿主链路。其中 4 个 edit-list 文件走 Reader 内部 PCM／裁剪路径，并再次校验完整输出；普通 MP4 Reader 返回压缩包时，由原 ABI 的 Decoder 接续解码，不能把 Reader 的压缩格式误判成浮点 PCM。

六个负例分别是缺失目标、重复 CCE、重复播放元素、长短窗不匹配、增益溢出、截断 CCE 头。全部返回 `HRESULT_FROM_WIN32(ERROR_INVALID_DATA)`，输出长度为零。

`boundary.cpp` 覆盖有声 Reset 后重放、逐字节截断／失败重试、Reset 恢复、失败 Initialize 清理及再次 Initialize、两条不同码流的两个 Decoder 并行各运行五遍、Reader Stop／Start 后 Seek(0) 完整重放。真实 Main 曾复现 Reset 多输出一帧；父任务在独立 Decoder 中缓存 ASC 并重新初始化状态后，恢复逐字节一致。没有修改 Reader 的有状态定位策略来规避测试。

平铺 `stage/run.cmd` 共 110 次程序调用：100 次正例、6 次负例、4 个边界程序。边界程序内部合计 `39 + 90 + 34 + 90 = 253` 项，整体为 359 项断言场景；本机全部通过。Stage 包含输入和已独立验证过的冻结输出，XP／Win7 按同一 DLL 和输出做精确兼容性回归。完整 Main 逐字节截断的本地扩展版本另外执行 1131 项。最后审查还对 960 帧独立 CCE 补测无 DRC、DRC ×2、单通道排除三组：均输出 10560 帧，倍数关系逐样本精确相等；这些附加测试不计入上述 stage 数量。DRC 固定表可能访问 960 帧未用的频谱尾部，因此临时 1024 项频谱按原 FAAD 行为完整零初始化。

最终候选验收 DLL SHA256：

`68c95cb8b51fd0efcb75510cb33dcdcead455918334058bd96b6e28981f6f4bf`

Stage 的逐文件哈希见 `SHA256SUMS.txt`；若父任务后续合并改变 DLL，需用新冻结 DLL 重跑，不能沿用此处旧哈希宣称验收新文件。XP／Win7 的最终执行结果由总验收报告记载，本文件不预先声明虚拟机已通过。

## 仍明确保留的边界

- 本轮 CCE 接入 AAC Main／LC。CCE 与 SBR／PS、LTP、SSR 的组合仍受控返回不支持，不能把新 SSR 基础解码支持等同于 SSR CCE 已完成。SSR 的独立合成不能复用普通 AAC IMDCT。
- 代码按现有 core 的 1024／960 帧长计算，但本轮外部参考向量集中于 44.1 kHz、1024 帧；960 帧仅补测上述 DRC 倍数关系，不能据此声称所有采样率／960 帧工具组合／CCE+PNS 已完成外部参考一致性验收。
- PNS 本身存在不同解码器随机序列选择差异；本轮未用含 PNS 的波形差异作为耦合正确性证据，也未修改随机序列来追逐单个参考哈希。
- stage 使用现有 x86／SSE2、MSVC、VC-LTL、YY-Thunks 兼容构建，没有通过外部新系统 DLL 实现耦合。
