# 未知长度 HTTP ADTS 输入

本轮仅扩展 Reader 的 ADTS 输入，不改变原版宿主接口、MP4 文件解析和已有本地文件读写路径。

当宿主 `IStream::Stat` 返回 `E_PENDING`／`E_NOTIMPL` 时，Reader 不再把输入当作空文件：

- 逐包检查 ADTS 同步头、7／9 字节头部、帧长和 CRC 头标志；帧长上限 8191 字节。
- 有界跳过 ID3v2 前置标签；流缓冲上限 64 KiB。
- 继续使用 FAAD2 2.11.3，保留预热、PCM 缓冲和对齐行为。
- 长度未知时返回时长 0；不宣称可定位或可写标签。Seek 返回 `E_NOTIMPL`。
- 正常帧边界结束返回 EOF；半个 ADTS 帧返回截断错误；宿主取消与传输错误不转换为 EOF。

宿主必须识别真实格式并提供顺序流。无需在 DLL 内增加 HTTP／TLS 客户端。
重建播放器从 `ttp_https` ABI 7 取得流并交给此 Reader。

本机 Windows 11、XP SP3、Win7 SP1 均验证了带长度与无长度的 HTTPS ADTS，另验证了截断帧。
已知长度 MP4/AAC 继续通过同一测试矩阵。测试代码保存在私有 `rebuild/tests`，不上传或加入 Actions。
原版播放器的本地 Reader ABI 未改变；不能据此认为缺少 `netacc.dll` 的原版播放器自动获得新版网络播放能力。

仍有边界：ADTS 中途改变采样率或声道仍由现有 AAC 核心拒绝；本轮 MPEG 广播的格式切换归一化不适用于 AAC。
