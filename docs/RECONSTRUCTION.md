# AAC 插件恢复记录

当前补全见 [Main／LC CCE](CCE_IMPLEMENTATION_20261030.md)、[SSR](SSR_IMPLEMENTATION_20261030.md)、[LD／960 帧](LD_VALIDATION_20261030.md)、[时钟／PCE／Reset](CORE_CLOCK_PCE_RESET_20261030.md)。之前的 [ADIF](ADIF_STREAMS_20261029P3.md)、[LD／ASC](LD_ASC_20261029P3.md)、[分片标签保存](FRAGMENT_METADATA_SAVE_20261029P3.md) 记录保留历史条件；旧限制中与新报告冲突的部分以新报告为准。

2026.10.30p1 后续恢复见 [多配置 MP4／无缝裁剪](MP4_CONFIG_SWITCH_20261030P1.md)、[LTP／DRC](LTP_DRC_VALIDATION_20261030P1.md)、[LTP 独立 CCE](CCE_LTP_COMPATIBILITY_20261030P1.md)。2026.10.30p2 补足了 [LTP960 真实 PCM 编码及三系统验证](LTP960_VALIDATION_20261030P2.md)。新记录取代此前对应的未支持／未验证描述，具体工具组合边界以各报告为准。

日期：2026-09-22；DLL 文件／产品版本现使用 [Actions 日期构建版本](BUILD_VERSION.md)。

## 范围与实现

本工程已生成可单独编译、替换使用的 `ttp_aac.dll`，不转发或加载原 AAC DLL。
恢复基于原插件的伪代码、机器码、GUID／虚表和实际运行对照。
首版使用与原二进制对应的 FAAD2 2.7 代码家族，现已升级到 FAAD2 **2.11.4**；
容器读取、宿主接口和 Nero 适配重新实现。新版解码核心不再承诺与原版 PCM 逐字节一致。
这不是原作者源码或 DLL 字节的复原，也不代表全部合法／损坏输入都已验证。

原插件：x86，314,720 字节；SHA-256：
`49195708ba9b942edd8215ffcb4e5db73a1548b0c02a5e2b11960ea12af69135`。
以下地址为原 DLL 首选基址 `0x60000000` 下的虚拟地址。

| 原实现 | 地址／证据 | 新源码 |
| --- | --- | --- |
| 四类 creator 枚举、导出 | `60006A97`、`600070D6` | `src/plugin.cpp` |
| MP4 打开、读包、定位 | `60005644`、`60005A5B`、`60005CAA` | `src/reader.cpp`、`src/mp4.cpp` |
| AAC 打开、读 PCM、定位 | `60001980`、`60001DFB`、`60001EF8` | `src/reader.cpp` |
| AAC 初始化、解码、重置 | `60006604`、`60006D71`、`60006A53` | `src/decoder.cpp`、FAAD2 |
| 首包 composition offset 对时长／定位的影响 | `600232A3`、`60022FB5` | 普通 MP4 时长从 mdhd 扣除首个 ctts；seek 时加回 |
| Init2 额外的帧长输出 | `600076E0`；调用点六次压栈、清栈 24 字节 | 根据 ASC 计算 960/1024、LD、SBR 帧长 |
| 音频格式选择 | `600060B2` | AAC 对象类型 `40/66/67/68`；MP3 `69/6B` 交给宿主 MP3 decoder |
| IAudioDataOffset | `600051CF`、`60023046`、`600234FA` | 返回最后一个 mdat box 起点的低 32 位，与原接口一致 |
| 流回调转发 | `60002224` | 查询 `1717A4A7-...` 扩展，通过 slot 4 转发 |
| Nero 创建／配置 | `60003CC7`、`600035FB` | `src/nero.cpp` |
| Nero 编码打开 | `60003709` | 同样的外部组件、接口 GUID、thiscall/stdcall 混合调用约定 |

Reader 的 Read 是 slot 14，**Seek 是 slot 15**；slot 13 是返回成功的 Stop。
返回的字符串和格式结构由 CoTaskMemAlloc 分配，调用方释放；不跨 DLL 释放 C++ 容器。
多实例各自持有容器索引、FAAD 状态和缓冲。

## 分片 MP4 修复

原问题文件含 52 个 `moof`、10,995 个 AAC 包。其传统采样表为空；旧 reader 打开成功后立即 EOF。
Win7 Media Foundation 也无法读该分片封装。

现在两种容器统一生成包含 64 位位置／时间、包大小、时长和 composition offset 的索引：

- 普通 MP4：`stsz/stz2`、`stsc`、`stco/co64`、`stts/ctts`。
- 分片 MP4：`mvex/trex`、`moof/traf/tfhd/tfdt/trun`。
- 处理默认值继承、显式 base offset、default-base-is-moof、带符号 data offset、多个 trun／traf／mdat。
- 检查 box／表大小、时间和偏移溢出、采样是否实际位于 mdat、截断、未知音轨及不支持的加密。
- 初版上限：moov/moof 64 MiB、1,000 万个采样包、单包 64 MiB；超限返回错误。
- 不依赖系统 AAC 解码或 Media Foundation；原问题文件无需转封装或修改。

沿用原 reader 的采样时间和预热语义；普通 MP4 的显示时长／定位使用原版 ctts 首包偏移规则，不另加 edit-list/gapless 裁剪。
composition offset 已解析；音频输出仍采用原 decoder 的顺序 PCM，不插入时间空洞对应的静音。

## 标签、封面与文件保护

普通 MP4 支持已知 ilst 字段、iTunes 自定义字段、ReplayGain，以及 JPEG/PNG 封面。
整数 gnre 使用一基编号映射到 ID3 流派表；Tracknumber／Disc 支持当前值及总数。
封面维持原接口的一张、写入最多 60,000 字节的限制。

标签和封面修改先暂存，最终 Release 或可选 MetadataCommit 接口只提交一次。
保存时先在文件末尾构造 free，再提交为新 moov，将旧 moov 改为 free；音频包和原音频偏移保持不变。
未知同级 box 保留，size=0 的末尾 box 转为显式长度，写失败尽量恢复原 moov。
不承诺断电或存储介质持续故障下的事务原子性。连续保存会增加 free 空间。
分片 MP4 的元数据／封面只读，不宣称支持其写回。
原 AAC 标签通过宿主 `CreateStdContent` 恢复；无该导出的最小宿主不能写裸 AAC 标签。

## 历史 FAAD2 2.11.3 基线

以下表格保留当时的结果；当前 2.11.4 与保存修正见 [2026-10-19 验证](VALIDATION_20261019.md)。

FAAD2 由 CMake 下载固定归档并校验 SHA-256，仓库保留完整许可证和适配脚本，不提交依赖源码。
静态内嵌适配、构建体积、旧系统验证和与 2.7 的数值差异见 [升级评估](FAAD2_UPGRADE_ASSESSMENT.md)。

| 样本 | PCM 字节数 | 当前 2.11.3 摘要 |
| --- | ---: | --- |
| 原问题普通／分片 MP4、ADTS及小缓冲 | 90,062,848 | `4d0690bb2b442a98` |
| 48 kHz | 770,048 | `ee13a24def846f4f` |
| 96 kHz | 1,540,096 | `becbd886db9f186c` |
| HE-AAC | 737,280 | `4f3421a515f7d199` |
| HE-AAC v2 | 737,280 | `8665fe2b41d45683` |
| 16 kHz | 262,144 | `23ca5b823a684403` |
| 单声道 | 712,704 | `86c65b9bb6d040a7` |
| 六声道 | 2,310,144 | `7da0c801c3d9fda` |
| Nero 编码后重读 | 753,664 | `42b5204ee2314aac` |

## 首版 FAAD2 2.7 验证记录（历史）

测试全部位于 `rebuild/tests`；没有加入本工程、源码包或 Actions。

### 首版与原插件的解码对照

使用 `/O2 /fp:precise /arch:SSE2`；与原 DLL 对照以下样本的完整 PCM 字节数与哈希均一致：

| 样本 | 输出 PCM 字节数 | FNV-1a 64 位摘要（测试探针约定） |
| --- | ---: | --- |
| 原问题文件的普通 MP4 对照 | 90,062,848 | `1b947262fc47b025` |
| 48 kHz AAC-LC | 770,048 | `841bc9db62f2e1a4` |
| 96 kHz AAC-LC | 1,540,096 | `f6565b31d0d0b7d` |
| HE-AAC | 737,280 | `cce1814131d7ef1a` |
| HE-AAC v2 | 737,280 | `892d725f3d9bd089` |
| 16 kHz AAC | 262,144 | `5a92ca84d1b3c88b` |
| 单声道输入（原解码器同样输出两声道） | 712,704 | `50dc65247111ccc3` |
| 48 kHz 六声道 | 2,310,144 | `7da0c801c3d9fda` |

原问题分片文件现在与普通 MP4 对照产生相同 PCM，时长 255,302 ms。
输出缓冲缩小至 4,000 字节仍可完整读取；修正原插件六声道在较小缓冲下定位后无法输出的问题。
上述哈希是回归对照标识，不是对所有码流逐样本一致的证明。

容器验证已覆盖 FFmpeg 生成的显式／默认偏移分片、多分片、多音轨、faststart，
以及多 trun 连续数据位置、tfdt v1、co64、stz2、末尾 size=0 mdat
在元数据修改前后 PCM 不变。非法包数、越界数据偏移、超大 moof、截断和未知音轨均返回错误，进程没有崩溃。
这些边界样本不能代替完整模糊测试或 FAAD2 安全审计。

### 旧系统

新 DLL 的每次构建都检查全部静态导入；当前通过 XP SP3／Win7 导出表校验，子系统版本 5.01。
Win7 SP1 x64 虚拟机已实际加载新 x86 DLL，并通过：原问题分片／普通 MP4／裸 AAC 完整解码，
HE-AAC、HE-AAC v2、六声道，开头／中间／末尾定位，Unicode 标签／PNG 封面写入与删除、Nero 编码和重读。
Win7 原问题文件 PCM 摘要仍为 `1b947262fc47b025`。

宿主中还已对照原 DLL 的 Nero float64 输入编码：两者产物解码后的 PCM 均为 753,664 字节、
摘要 `40168d4088c37079`。恢复 ctts 规则后显示时长均为 2,089 ms。
Nero 配置窗口已实际弹出并取消，取消返回 `0x8BDA0605`，与原适配器约定对应。
移除 Nero 组件后 AAC 解码仍通过，创建编码器由宿主明确返回缺少模块 `0x8007007E`。

XP 虚拟机恢复后，已在 XP SP3 x86（5.1.2600、SSE2）完成同一发行 DLL 的实测：
原问题分片／普通 MP4／裸 AAC、48／96 kHz、16 kHz、单声道、HE-AAC／HE-AAC v2、六声道完整解码与定位通过，
PCM 摘要与上表一致；4,000 字节缓冲完整输出不变。普通 MP4 Unicode 标签／PNG 封面写入重开及删除、
裸 AAC 标签、Nero 编码重读和配置窗口取消均通过。裸 AAC 标签用例不包含 PNG 封面写入。

XP 原版 5.7.9 宿主中，新插件实际播放普通 MP4、裸 AAC 和原问题分片 MP4，约四秒的保存进度分别为
3,940／3,930／3,930 ms；旧插件普通 MP4 为 3,910 ms，问题分片为 0。
重建版 XP／Win7 Release 使用新插件播放分片文件为 3,970 ms，启动退出检查通过。
模块路径已确认，最终没有错误对话框或强制结束。这些短时宿主检查不等于原版所有界面流程均已验收。

原插件对照所需的 MSVCR110.dll 仅补入其隔离测试目录；新插件不依赖它。
Nero 配置探针补上 XP 标准控件初始化后通过，失败点原先位于插件接口调用之前。
本轮没有修改发行 DLL，其 SHA-256 仍为 `95e28b7b68db61b07f40612a016fdc25a018239e3b4869914e5fa3ca43b3c5ff`。

## 尚未完全覆盖的原版分支（更新至 2026.10.29p3）

- ADIF LC 已覆盖合成单／双／六声道、CBR／VBR、定位和取消恢复；历史 ADIF 编码器曲库及其它 Profile 仍未完整覆盖。
- Main 单声道、LTP 和代表性 LD 已验证；Main channel coupling、SSR、表外核心频率、扩展 AOT 及全部声道组合仍未补齐。LD 已修正旧 TNS 表，数值不以模仿原库错误为目标。
- 不可定位 ADTS／ADIF 已支持顺序播放，保留参考时长接口；不承诺任意 Seek、低延迟直播或在线 DASH。ADIF 本地 VBR 时长仍是码率估算。
- MP3-in-MP4 识别与交接已实现，仍依赖宿主 MP3 decoder，未列为本轮已完整对照的格式。
- 动态 ASC 变化、多 sample description、DRM 不支持；在线 DASH 不是本地 fragmented MP4 的同一功能。
- Nero 编码算法和配置窗口来自原外部组件，本工程恢复的是适配层；不含这些组件的源码或二进制。
- 标签修改暂存，最后一次释放 Reader 或显式 MetadataCommit 时提交。分片写回限于现有标签区与预留空白，空间不足返回错误；不移动任意分片布局，也不承诺介质持续故障下的原子保存。

因此本版可以替换已验证的 AAC/MP4 播放路径并修复本次分片文件问题；不能标为“全部原版功能百分之百复原”。
