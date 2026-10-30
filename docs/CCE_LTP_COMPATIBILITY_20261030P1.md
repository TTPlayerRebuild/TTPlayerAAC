# LTP 独立耦合与 CCE 标准边界修正（2026-10-30p1）

本文接续 `CCE_IMPLEMENTATION_20261030.md`。本轮完成 AAC LTP 独立耦合的工具级兼容实现，并把“待实现的组合”和“标准禁止的组合”分开。没有修改公开 AddIn／Reader／Decoder ABI，没有调用原版 DLL，没有启用 FAAD 的 CCE 跳过分支。

## 标准范围

| 组合 | 本轮处理 | 依据和限制 |
|---|---|---|
| Main／LC 现有三个 CCE 位置 | 保留，继续回归 | 上轮官方 ISO／FATE 及 FFmpeg 对照结果仍有效 |
| AAC LTP 独立 CCE | 补齐解码和历史状态，按工具级兼容验收 | 完整解析真实预测残差，源与目标分别维护预测历史；不据此宣称某个 profile／level 已认证 |
| AAC LTP 依赖 CCE | 返回错误 | ISO/IEC 14496-3:2001/Cor.2:2004 §4.6.7.4.3、14496-26:2024 §7.4.1.2.2.6 明确禁止；不是解码器待补功能 |
| SSR CCE | 保持受控拒绝 | SSR 的标准工具范围不包含耦合；14496-3:2009 表 1.5 的 SSR 最大独立／依赖 CCE 数均为零，不能直接拿普通 AAC IMDCT 给 SSR 使用 |
| 同一 CCE 重复指向同一 SCE／CPE | 新增错误返回 | 14496-26 §7.4.1.2.2.6 明确禁止。SCE tag 0 与 CPE tag 0 属于不同元素命名空间，仍可同时被指向 |

标准中的“工具语法”和“profile／level 最大通道配置”不能混为一谈。14496-3:2009 表 1.5 的 AAC LTP 行仍写作 `5.1.0.1`（A.L.I.D），而后续一致性规范又明确禁止 LTP 的依赖 CCE。因此这里不把自行重组的独立 CCE 向量称为某个 profile／level 的官方认证向量，也不借此放开依赖 CCE。SSR 的拒绝有标准边界支持，不再笼统列作“尚未恢复的 SSR CCE”。

参考：[ISO 14496-3:2001 Cor.2](https://previewnorm.com/iec/ISO%20IEC%2014496-3-2001%20COR2-2004%20PDF.pdf)、[ISO 14496-26:2024 一致性条款](https://previewnorm.com/iso/ISO%20IEC%2014496-26-2024%20PDF.pdf)、[ISO 14496-3:2009 表 1.5](https://ossrs.io/lts/zh-cn/assets/files/ISO_IEC_14496-3-AAC-2009-90cda427e845cc176293bffee3f92f99.pdf)。

## 实际修复

1. `ttp_cce_enabled` 和解析入口允许 AAC LTP 的独立耦合，依赖标志为零时仍立即拒绝。SSR、ER 语法和 CCE＋SBR／PS 的既有边界没有通过取消检查来放开。
2. 独立 CCE 源使用自己的私有 decoder 完成 LTP 预测、TNS、无 DRC 的 IMDCT 和 `lt_update_state`。Main／LC 原先在保存频谱后提前返回；对 LTP 沿用这个早退会丢掉后续所有预测历史。给各目标声道应用 DRC 的独立输出重叠仍另行保留。
3. 普通目标声道先更新自身 LTP 历史，再混入独立 CCE。否则其他声道的耦合信号会进入目标预测环路，后续波形不断偏离。SCE 与 CPE 两条路径都修正。
4. 解析每条 target 时检查 `(元素类型, tag)`，拒绝同一 CCE 内的重复目标。不同 CCE 指向同一目标、不同元素类型使用相同数字 tag 都保持有效。
5. 调试发现普通 LTP 也存在 DRC 污染预测历史的问题。本轮配套 `faad2_ltp_drc_compat.cmake` 在 DRC 前用独立重叠维护无增益预测历史，最终输出继续使用已启用的 DRC。CCE 源与目标均通过集成验证，不能全局关闭 DRC 来换取参考一致。

前三项和重复目标检查仅修改 `src/faad_coupling/` 与 `cmake/faad2_coupling_compat.cmake`；所有上游修改只发生在 FAAD2 2.11.4 的私有构建副本。独立源保留真实 `ttp_core_rate`，未把 7350 Hz 当成 8000 Hz 输出。构建继续为 x86／SSE2、现代 MSVC、VC-LTL 和 YY-Thunks；无额外运行时 DLL。

## 真正使用预测的样本与负对照

证据只放在本地 `rebuild/tests/plugins_followup_20261030p1/aac-coupling/`，不进 Actions 或发行包。

源数据来自固定官方 FAAC 1.28 编码器的公开 API，48 kHz 和 8 kHz 为实际有声编码，源包确实包含启用 LTP 的 ICS，不能用只把 AOT 写成 4、从未启用预测的文件替代。7350 Hz 测试复用 8 kHz 的同一合法频带族，并将 ASC／容器时钟改为 7350 Hz；这是明确注明的衍生向量。测试 ASC 显式声明没有 SBR。

`extract.py` 在私有解析器中记录 ICS 的精确位边界，保留编码器产生的完整预测参数和残差信息；`generate.py` 只重组 SCE／CPE／CCE 头、目标和增益。两端同时有声的向量同时验证耦合源和目标的预测状态。FAAC 1.28 默认 TNS 曾产生超出非 Main 上限的阶数，因此测试通过其公开 `useTns=0` 设置重新编码，未修改编码器源码，也未删除解码报错的帧来拼凑通过结果。

可复用输入 SHA256：

| 文件 | SHA256 |
|---|---|
| `active-ltp-48000-1.packets` | `c02fa54448da8434d32a0b61bcc961453e0cdb65d70280ea68c11a82e1ad3ac1` |
| `active-ltp-8000-1.packets` | `2932e781d1cbf03b40657d65bcb0300578e8fbfbee09976bf7b0601768c6f7d7` |
| `active-ltp-7350-1.packets` | `85c005f01dcebb1ebf2b7114499ff781c6b2664a466cc0e225adc0f171bf74e6` |

无 DRC 的完整 PCM 与 [FFmpeg AAC 解码器](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aac/aacdec.c) 独立对照，不只比较时长。48 kHz 单路最大误差约 `9.2082e-5`、RMS `1.1822e-5`；同一真实信号同时作为目标及耦合源时最大误差约 `1.8416e-4`、RMS `2.3643e-5`。这是既有 LTP 定点历史舍入误差叠加后的量级，未因耦合新增整段漂移。8 kHz／7350 Hz 单路最大误差约 `4.7088e-5`。已按公开 Decoder 固有的首个音频帧预热去除 1024 帧，没有通过任意偏移搜索对齐。

两个故意恢复错误实现的私有 DLL 提供算法负对照：

| 恢复的错误 | 同一 48 kHz 双端有声向量的误差 |
|---|---|
| CCE 源保存频谱后立即返回，不维护 LTP 历史 | 最大 `0.3740982`，RMS `0.1056337` |
| 将 CCE 加入目标后才更新目标 LTP 历史 | 最大 `1.3420480`，RMS `0.6694122` |

`control-results.json` 记录这两份私有 DLL 的 SHA256。这证明新增测试能发现漏实现的历史环节，不能仅修改支持标志就通过。

另外，旧生产 DLL 及未加重复目标检查的私有候选会接受重复 target；修正后返回 `ERROR_INVALID_DATA`、无输出。两条 Main／LC 正例同时使用 SCE tag 0 和 CPE tag 0，完整 PCM 与 FFmpeg 最大误差 `2.1828e-11`，证明检查没有误伤元素命名空间。

## 集成验收

新 stage 有 26 个正例：24 个 LTP CCE 向量（3 种时钟，各覆盖 SCE／CPE、源与目标均有声、左右不同增益、动态路由、DRC ×2、排除左声道）及 2 个合法同数字 tag 向量。

DRC 的固定 ×2 和排除通道输出，均与已独立验证的无 DRC PCM 逐样本精确满足倍数关系，目标自身也启用 LTP 的情况包括在内。此前目标 DRC 反馈污染导致峰值约 4.09，修复后回到预期约 1.539；未关闭 DRC 或截掉有效尾音。

`stage/run.cmd` 共 **65 次程序调用**：

- 26 正例各运行独立 Decoder 和 Reader → Decoder，共 52 次。
- 5 个错误输入：Main／LC／LTP 重复目标、LTP 依赖耦合、SSR CCE。
- 每个错误输入另做 9 项错误锁存、再次读取、Reset／重新 Initialize、完整 PCM 恢复，共 45 项。
- 3 个状态边界程序分别通过 68、60、61 项：有声 Reset 精确重放、截断／失败重试、两个 Decoder 并行、Reader Stop／Start 后 Seek(0) 重放。

本机新 stage 全部通过。上述程序内展开合计 291 项断言场景。上轮 110 次 Main／LC 程序调用另用同一集成 DLL 全部通过，旧 golden PCM 没有重制。关键证据为 `stage-results.json`、`stage-local.log`、`previous-results.json`、`negative-results.json`、`control-results.json`。

当前集成验收 DLL SHA256：

`797175aedd2f538f4a6b5cc03e6e27038128fe064c67b2a462f7275f1b381369`

平铺 stage 为 90 文件、16,918,540 字节，逐文件哈希见 `stage/SHA256SUMS.txt`。XP／Win7 由父任务使用同一平铺目录验收；本文件不预先声明虚拟机通过。后续若 DLL 再次合并变化，必须替换 DLL 后复验，不能沿用此哈希宣称测试了另一个文件。

最终候选仅替换 stage 内的 DLL 后重新运行上述 65 次调用，全部通过；所有输入和 golden PCM 的 SHA256 与替换前一致，结果见 `final-local-run.log`。没有为最终 DLL 重新保存参考 PCM。

另提供宿主实际播放向量 `host/ltp-independent-cce-12s.m4a` 与 `host/ltp-independent-cce-12s-drc2x.m4a`。两者为连续 FAAC 编码后的 564 个音频包，容器时长 12.032 秒、公开解码 PCM 12.0107 秒，不是把已有压缩帧直接循环拼接。前者完整 PCM 对 FFmpeg 最大误差 `9.2082e-5`、RMS `9.3256e-6`；后者逐样本精确为前者两倍，峰值约 0.863，避免宿主正常浮点转整型输出时削波。这两个附加样本均通过最终 DLL 的 Decoder 与 Reader → Decoder，未计入 65 次 stage 数量。

## 保留的限制

- 本轮没有为 LTP 依赖 CCE 或 SSR CCE 增加“成功”路径，也不把这种拒绝记为未完成算法。
- 独立 LTP CCE 是基于真实预测残差及独立参考的工具级兼容，不等于 profile／level 认证。未找到官方成品 LTP CCE 音频时，不把重组向量冒称 ISO 原始测试流。
- 新的外部正确性覆盖为 1024 帧、长窗及编码器实际使用的工具；没有据此声称 960 帧、CCE＋PNS、SBR／PS 的所有组合已验收。已有 Main／LC 960 帧 DRC 回归仍沿用上一轮明确限定的覆盖。


## 三系统收尾记录

2026.10.30p1 冻结 DLL `797175aedd2f538f4a6b5cc03e6e27038128fe064c67b2a462f7275f1b381369` 已在 Win11、XP SP3、Win7 上分别通过本节 65 次公共接口调用。三系统同时通过每系统 1577 次整合回归。具体实际宿主、发行包及偶发退出观察以 `rebuild/docs/AUDIO_PLUGIN_FOLLOWUP_20261030P1.md` 的收尾记录为准。Win10 按用户约定由 Win11 代测，不声称实际运行了 Win10。
