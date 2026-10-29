# AAC 部分读取取消、MP4 提示字段与历史 Profile 验证

日期：2026-10-29；构建版本：`2026.10.29p2`。本轮接续 [呈现时间与分片编辑验收](PRESENTATION_TIMELINE_20261025.md)，修复两处已经复现的问题，补充 Main／LTP／LD 的实际样本证据。没有修改 FAAD2 2.11.4、原始 DLL 或用户音频。

## 1. 实时 ADTS 在部分读取后取消会丢失数据

`AudioReader::fill_live` 原来先检查 `IStream::Read` 的 HRESULT，再追加 `pcbRead` 返回的字节。可复现的宿主流在一次读取中已经消耗并交付部分数据，随后返回 `E_ABORT` 或 `ERROR_CANCELLED`；旧实现直接抛出错误，已经交付的字节没有保存。实时流不能倒退，重试后便丢失 AAC 包中的字节，最终发生解码错误。

修正后先验证 `got <= 请求长度`，保存已交付的数据，再向宿主返回取消错误。失败读取不会被当作 EOF；恢复读取沿用原来的包缓冲和 FAAD 状态。取消时仍返回空输出缓冲，没有把未完成包交付给宿主。

本地长度已知的流走另一条路径：`read_at` 在重试前重新定位到包的原始位置，不需要使用实时流的保留策略。这条路径没有改动，但一并测试，避免把仅对实时流成立的结论套用到本地文件。

原插件接口地址仍以 [恢复记录](RECONSTRUCTION.md) 为依据：裸 AAC 的 Open／Read／Seek 对应 `60001980`、`60001DFB`、`60001EF8`。本轮的取消修复是重建实现的状态恢复修正，没有声称原二进制具备完全相同的实时流扩展。

## 2. 有效的高采样率 Main 被 MP4 提示字段提前拒绝

官方 [FFmpeg FATE 的 am00_88.mp4](https://fate-suite.ffmpeg.org/aac/am00_88.mp4) 是 88.2 kHz、单声道 AAC Main，ASC 内含 PCE。其 `mp4a` sample entry 的 rate 为 0，channel count 提示为 2，实际解码格式由 ASC／PCE 确定。旧重建实现先执行 `require(rate && channels)`，尚未调用 FAAD 就返回 `ERROR_INVALID_DATA`。

修正内容：

- AAC 的 sample-entry rate 为 0 时，从现有解码器支持的**索引式 AAC core 采样率配置**恢复容器计算使用的时钟。16.16 sample-entry 字段不能直接表示 88.2／96 kHz，这类缺失提示不能单独作为拒绝原因。
- AAC 的 sample-entry channel count 可以为 0；实际输出格式仍由 `AacCore::config` 和首个解码包验证。PCE 的 channel configuration 为 0 也不等于“没有声道”。
- MP3-in-MP4 仍要求原有 rate／channel 字段有效，没有扩大它的解析范围。
- 缺失提示的补全不代替 ASC、包边界或实际解码格式验证；损坏 ASC 仍受控拒绝。

SBR 的输出频率可能是 AAC core 时钟的两倍。本轮保留 `iTunSMPB` 与既有 core 时钟的对应关系，之后才由 Reader 使用实际 PCM 频率换算输出。曾测试过直接使用 FAAD ASC 查询返回的有效输出频率，这会令零提示字段的 HE 样本裁剪为半长，已经撤销。最终 HE／HEv2／SMPB 零字段用例与原提示字段文件的 PCM **逐字节相同**；私有目录早期 `he-zero.pcm` 不是验收参考，也未进入 VM stage。

该 Main 样本修复后完整输出 5,780,480 帧。与 FFmpeg 9.0.1 独立解码输出在扣除原有首包 1,024 样本抑制后比较，最大绝对差 `2.384185791015625e-7`，RMS `3.9201048079597966e-8`。没有为追求微小浮点差修改运算路径。

这里是重建版的有效输入扩展：原始 `5.790/AddIn/ttp_aac.dll` 对该样本实际返回 `0x8BDA0006`，不能将新支持描述为原版已经可以播放。

## 3. ASC 与历史 Profile 的边界

这次没有实现完整 AudioSpecificConfig 解析器。FAAD2 2.11.4 的 `NeAACDecAudioSpecificConfig` 对以下输入仍不支持：

- core 频率索引 15 的 24-bit 显式频率：上游实现读取但丢弃频率值，随后 `get_sample_rate(15)` 返回 0；实测 ASC 查询及 `Init2` 均返回 `-2`。
- AOT 31 引出的扩展对象编号：上游未解析该扩展，实测 ASC 查询及 `Init2` 均返回 `-1`。

两类配置分别构造有／无 sample-entry 提示字段的用例，共四项；旧 `2026.10.25` 与本轮 DLL 都返回 `ERROR_INVALID_DATA`。它们作为明确的既有限制记录，不算新增加的格式支持，也不把所有这种编码配置称作损坏文件。

历史 Profile 实测使用官方 [FATE AAC 样本](https://fate-suite.ffmpeg.org/aac/)；使用原 AddIn 的四工厂接口进行对照，未修改原插件。

| 样本 | 重建版结果 | 原版及独立实现观察 |
| --- | --- | --- |
| `am00_88.mp4`：Main、88.2 kHz、单声道 PCE | 本轮修复容器提前拒绝；全曲通过 | 原版 Open=`0x8BDA0006`；与 FFmpeg 对齐后仅有上述小浮点差 |
| `ap05_48.mp4`：LTP、48 kHz、立体声 | 旧／新重建版均完整输出 911,360 帧，PCM 不变 | 原版 Open=`S_FALSE`，继续调用 Format／Decoder 后同样完整输出，不能将 S_FALSE 判作“不支持” |
| `am05_44.mp4`：Main、44.1 kHz、6 声道 | FAAD 首包返回 error 6：`Channel coupling not yet implemented`，仍不支持 | 原版 Open=`S_FALSE` 且 Format／Decoder 初始化成功；探针在 128 次连续空输出后停止，只记录该观察，不据此断言宿主所有路径行为 |
| `er_ad6000np_44_ep0.mp4`：ER AAC LD、44.1 kHz、6 声道 | 第 35 包长度 552，FAAD 报告消耗 553 字节，触发 `consumed <= packet` 保护 | 原版可完整输出 154,624 帧；直接 FAAD 输出与原版很接近，但二者与 FFmpeg 存在明显差异，尚未用标准参考判定哪方正确 |

LD 直接库调用报告 `info.error=0`，却在多个包上出现消费计数超过包长。按 MPEG→Windows 顺序统一声道后，原版与 FAAD 的最大差约 `3.58e-7`；两者与 FFmpeg 的最大差约 `0.107935`、RMS 约 `0.001092`。这些结果不足以单凭 FFmpeg 判定原版／FAAD 错误，但也不能将超出包边界解释为已经验证无害的填充。本轮**保留完整性检查**，没有钳制消费数或放过此类包；LD 的兼容范围继续列为未完成。

SSR 在上游 `common.h` 中仍未启用，本轮没有无证据打开其编译分支。ADIF、其它历史 Profile／声道组合以及分片标签写回也没有因此完成。

样本 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| `am00_88.mp4` | `83e83c47115f443a8707412c1deb2c04ddd7258c52f38595b934ba1290767d09` |
| `am05_44.mp4` | `c2c82c447661b4e8fc1da278fffb19fc822e0fd8e0db912bfe4f7bfa2af280ae` |
| `ap05_48.mp4` | `8dc2c9a9f65c1fb7a6a797a4a9fd3574f4bffc1fa9d0b6e4550e9c530e4e2a76` |
| `er_ad6000np_44_ep0.mp4` | `dcc229f067ed9c8c63617d4024f18d6259b8aeabf9ebcfd7a55e8c1f27da2f50` |

原 DLL SHA-256：`49195708ba9b942edd8215ffcb4e5db73a1548b0c02a5e2b11960ea12af69135`。

## 4. 最终构建和回归

仅生产代码 `src/reader.cpp`、`src/mp4.cpp` 有修改；ABI、FAAD2 版本和构建依赖不变。使用 `pwsh -NoProfile -File ttp_aac/build.ps1 -PackageVersion 2026.10.29p2` 构建。

- DLL 文件／产品版本：`2026.10.29p2`。
- 大小：397,312 字节，与 `2026.10.25` 相同。
- SHA-256：`850d049e19dcc35220c894fe79e3af0303e9faeaebc05c9f668e3496a443ee4b`。
- x86、子系统 5.01；XP／Win7 导入检查通过，仍为 3 个系统 DLL、92 个导入；没有新增运行时解码 DLL。

同一最终 DLL 已在 Windows 11、XP SP3、Win7 SP1 分别执行以下回归，每个系统 238 项，共 714 项通过：

| 验证组 | 数量 | 结果 |
| --- | ---: | --- |
| live／known-length × 1／31／8192 字节短读 × 两类取消 HRESULT × 0／1／7／13／128 已读字节 × 1／2 次连续取消 | 120 | 全部恢复，PCM 与对应无故障基线逐字节相同 |
| Main、PCE 零提示、损坏 ASC、LTP、LC／HE／HEv2／SMPB／6 声道编辑零提示 | 9 | 有效输入 PCM 一致，损坏 ASC 正确拒绝 |
| 显式频率／扩展 AOT，有／无提示字段 | 4 | 既有限制受控拒绝 |
| 上一轮完整时间轴／定位／保存／只读回归 | 105 | 全部通过 |
| 每个系统合计 | **238** | 三个系统分别通过 |

所有新测试、下载样本、原版对照和日志均在私有 `rebuild/tests/plugins_followup_20261029p2/aac`。测试未加入工程、Actions、运行包或源码发行包。

发行核对另发现 `build.ps1` 对允许目录整树复制，会把本地 `cmake/__pycache__/*.pyc` 带入源码附件。已改为在允许目录内逐项复制并排除缓存、编译产物和重解析点；项目源码和固定 FAAD2 对应源码仍完整提供。运行 ZIP 继续只有 DLL 与 SHA256SUMS，已测试的 DLL 字节未变。

`vmstage/run.cmd` 为 133 项新边界的平铺验收入口，包含同一 DLL、两个 XP 兼容探针、必要样本及去重的期望 PCM、`manifest.json` 和 `SHA256SUMS.txt`，约 36.9 MiB。没有打入中间实际 PCM 或早期失败实验文件。旧 105 项在本轮私有 `previous105` 副本执行，最终日志为 `previous105-final.log`。

主任务提供的 XP／Win7 核心日志为私有目录下的 `xp-result.log`、`win7-result.log`（新 133 项）与 `xp-previous-result.log`、`win7-previous-result.log`（旧 105 项）。它们使用上述最终 DLL，不是对静态导入检查或上一轮结果的替代引用。

主任务另在 Windows 11、XP SP3、Win7 SP1 的原版／重建版实际宿主分别播放 Main 88.2 kHz 和 20 秒分片样本，每系统四次、合计十二次运行通过。检查包括实际加载上述 DLL、请求文件进入播放、时间推进、无错误对话框及正常关闭；分片样本播放超过 10 秒，越过其中的空白区间。结果保存在私有父目录 `integration/aac-modern-host-results.json`、`aac-xp-host.log`、`aac-win7-host.log`。宿主使用静音设置，未将这些结果计为实体音频设备听音测试；Windows 11 代为验证现代系统，没有独立 Windows 10 实机结果。
