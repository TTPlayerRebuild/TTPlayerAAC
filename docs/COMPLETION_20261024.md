# ttp_aac 后续补全与验收

日期：2026-10-24。发行版本：`2026.10.24`。

## 修改

非碎片 MP4 支持 iTunSMPB 及单段 elst v0/v1 裁剪。有效裁剪时使用 Reader 内部 PCM 路径，原工厂／虚表保持不变；首个 FAAD 预热只计时一次。按 FAAD 声道位置重排并提供 PCM 掩码。

## 验证

LC／HE／HEv2、单／双／六声道及 co64 用例通过；137 样本非帧对齐偏移的 elst v0/v1／iTunSMPB 结果逐字节等于基线切片；原版与重建版均实际播放通过。

Windows 11、XP SP3 x86、Win7 SP1 的组合回归各 33 项通过，其中 19 组 PCM 样本数及校验相同。这是组合测试数量，不代表本插件单独具有 33 个独立用例。Win11 按用户要求代为现代 Windows 验证，无独立 Win10 实测。

## 保留边界

本轮验收时多段／空编辑列表、碎片 MP4 复杂编辑和全部历史 Profile 尚未补齐。后续多段／空编辑列表的源码补全与新增本地测试见 [编辑列表补全记录](EDIT_LIST_COMPLETION_20261024.md)，不将本文原有三系统验收范围扩大到后续构建。无有效裁剪数据继续旧压缩 Reader＋Decoder；固定 Decoder ABI 输出容量没有被越界扩大。

测试代码和样本只保存在工作区 `rebuild/tests/plugins_complete_20261024`，不进入 Action 或发行 ZIP。运行 ZIP 仅含 DLL 与 SHA256SUMS；许可证留在仓库及既有对应源码包。本轮没有升级上游依赖版本或增加外部解码 DLL。

DLL 大小：393216 字节。SHA-256：`68357dd30e027c539ee2d746c8b806de2454873fb8ebd5a0055a5c7c7b8b35fb`。

总报告位于工作区 `rebuild/docs/AUDIO_PLUGIN_COMPLETION_20261024.md`；本文补充早期文档，不将旧轮次的通过范围扩大到未测分支。
