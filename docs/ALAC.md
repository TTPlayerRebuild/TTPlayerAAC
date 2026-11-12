# ALAC / MP4A 扩展

MP4 Reader 新增 `.alac`、`.mp4a` 合法后缀。它们是 ISO BMFF/MP4 音频文件的别名，不代表支持无容器、无配置的任意裸 ALAC 字节流。

解析器分别处理 `mp4a/esds` 和 `alac/alac` sample entry（也接受 wave 包装的配置）；不再假定所有 MP4 音频都是 AAC。
ALAC 使用固定 FFmpeg 9.0.2 的最小静态 decoder + avutil；FAAD AAC Reader/Decoder 和现有编码器工厂不变。

ALAC Reader 直接输出 PCM，不需要宿主另找一个 Decoder：16/20/24/32 有效位整数，20 bit 用 24-bit 左对齐容器，32 bit 不经 float。
FFmpeg ALAC 核心已将 ALAC 元素顺序映射为其真实 native speaker mask，插件按该顺序交织，不套用猜测的标准声道顺序。
沿用 MP4 edits、fragment、标签、封面和 Commit；每个实际引用的配置必须保持采样率、声道布局与位深一致，才能发布一个固定宿主格式。
Seek 到独立 ALAC 包并在真实 PCM 内裁剪；不重放整曲，也不为最终 edit 的 movie-clock 舍入补假样本。

`cmake/build_alac.py` 使用独立生成目录，不修改传入 FFmpeg 源码。默认下载固定归档并校验；离线使用 `TTP_FFMPEG_SOURCE` / `TTP_FFMPEG_MAKE` / `TTP_FFMPEG_BASH`。
`build.ps1 -SourcePackage` 同时携带 FAAD 与该次 ALAC 构建的完整 FFmpeg 源码、GNU make 归档及许可，以便重建/重新链接。
LGPL profile 检查不替代 FAAD 与整个插件既有许可要求，参见 `third_party/` 和主 README。
