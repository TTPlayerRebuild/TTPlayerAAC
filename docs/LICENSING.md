# 第三方许可与源码分发

## 仓库中保留什么

项目自有代码的 MIT 许可见根目录 [LICENSE](../LICENSE)。该许可不替代第三方组件的许可。

| 组件 | 版本 | 仓库内的许可与来源 |
| --- | --- | --- |
| FAAD2 | 2.11.4 | [完整 GPL 文本](../third_party/faad2/COPYING)、[作者](../third_party/faad2/AUTHORS)、[来源及修改记录](../third_party/faad2/ORIGIN.txt) |
| VC-LTL | 5.3.1 | [完整许可文本](licenses/VC-LTL-LICENSE.txt) |
| YY-Thunks | 1.2.2 | [完整 MIT 许可文本](licenses/YY-Thunks-LICENSE.txt) |

FAAD2 源码不提交到 Git。CMake 从固定的 2.11.4 标签下载归档，校验 SHA-256，
再对构建目录内的副本应用静态链接及 MSVC 兼容适配。原始下载缓存保留原作者声明。
修改后的两个头文件包含修改说明与日期；适配脚本和来源记录保存在仓库。

Code from FAAD2 is copyright (c) Nero AG, www.nero.com

## 版权声明不能代替许可证

FAAD2 源文件声明采用 GPL 第 2 版或后续版本。GPL 第 1、2 节要求保留版权、许可和免责声明，
附完整许可证，并标明文件修改；第 3 节涉及目标代码分发时对应源码的提供。
参见 [FAAD2 随附许可](https://github.com/knik0/faad2/blob/2.11.4/COPYING)
和 [源文件许可声明](https://github.com/knik0/faad2/blob/2.11.4/libfaad/common.h)。

因此可以不把依赖源码提交到本项目 Git，但不能把许可文本删成一行作者信息，
也不能把包含 FAAD2 的 DLL 当作仅受根目录 MIT 约束的产物分发。
构建时从第三方服务器下载源码，不会免除二进制分发时提供对应源码的要求。

## 发行文件

- `ttp_aac-版本号.zip`：仅 `AddIn/ttp_aac.dll` 和 DLL 的 `SHA256SUMS.txt`。
- `ttp_aac-版本号-source.zip`：本项目源码、构建及适配脚本、固定版本 FAAD2 解码源码和完整许可。
- 外层 `SHA256SUMS.txt`：上述两个 ZIP 的校验值。

Actions 勾选 Release a Version 时一起发布这些附件，并在说明中链接许可、源码和构建提交。
源码包中的 `third_party/faad2/source` 直接供 CMake 使用；从源码包重新构建无需再次下载 FAAD2。
本地测试、样本、原插件及私人分析材料不加入源码包，Actions 不运行本地测试。
