# ttp_aac 安装

关闭播放器，将插件 ZIP 解压至播放器目录，使 `AddIn/ttp_aac.dll` 覆盖原同名插件。
普通版及 XP SP3／Win7 版使用同一 x86 DLL，CPU 需要 SSE2。

本版修复本地分片 MP4 的读取。AAC 解码不依赖系统 Media Foundation 或 Nero。
需要 Nero 编码时，保留原 `AddIn` 中的 `Aac.dll`、`aacenc32.dll`、`NeroIPP.dll`。
它们不包含在此插件包内。

详细恢复与验证见同目录 [RECONSTRUCTION.md](RECONSTRUCTION.md)。
分片 MP4 的标签／封面仅支持读取。XP SP3 x86 与 Win7 SP1 x64 已完成实际解码、定位、标签／PNG 封面和 Nero 编码验证；XP 中也已用原版 5.7.9 和重建版实际播放问题分片文件。

发行包名为 `ttp_aac-yyyy.MM.dd.zip`，同日再次发布追加 `p1`、`p2` 等后缀，与 rebuild 一致。
DLL 的文件版本和产品版本与包名中的版本一致；例如 `2026.09.22p1`，Windows 四段数字版本为 `2026.9.22.1`。
ZIP 内仅包含 `AddIn/ttp_aac.dll` 和校验该 DLL 的 `SHA256SUMS.txt`。
Release 附件中的另一份 `SHA256SUMS.txt` 用于校验运行 ZIP 和独立源码 ZIP。
FAAD2 已升级为 2.11.4，源码在构建时下载，不提交 Git；完整许可证和适配脚本保留在仓库。
`ttp_aac-版本号-source.zip` 提供实际使用的 FAAD2 解码源码及本工程构建脚本，见 [许可说明](LICENSING.md)。
现代工具链生成的同一 DLL 兼容 XP／Win7，用户无需安装 VC++ 2012、VC-LTL 或 YY-Thunks。
