[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-AacBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-AacVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Missing or invalid repository/commit.'
}
$archiveName = "ttp_aac-$version.zip"
$archive = Join-Path $ArtifactDirectory $archiveName
$sourceName = "ttp_aac-$version-source.zip"
$sourceArchive = Join-Path $ArtifactDirectory $sourceName
$manifest = Join-Path $ArtifactDirectory 'SHA256SUMS.txt'
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHash = (Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedHashes = (Get-Content -LiteralPath $manifest -Encoding UTF8 -Raw).Trim() -replace "`r", ''
if ($expectedHashes -cne "$hash  $archiveName`n$sourceHash  $sourceName") {
    throw 'AAC package SHA-256 verification failed.'
}
# Version was fixed before compilation. Never rename an already-built package.
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" }
          else { "$repoUrl/commits/$version" }
$notes = @"
**完整更新日志**: $logUrl

下载并解压 ttp_aac-$version.zip，关闭播放器后将 AddIn/ttp_aac.dll 复制到千千静听安装目录。
同一 x86 DLL 支持普通版及 XP SP3／Win7，CPU 需要 SSE2。
ZIP 仅包含 AddIn/ttp_aac.dll 和 SHA256SUMS.txt；Nero 编码组件需保留播放器原有文件。
对应源码另见 ttp_aac-$version-source.zip，包含 FAAD2 2.11.4 原始解码源码、适配脚本及完整许可证，不含本地测试。
Code from FAAD2 is copyright (c) Nero AG, www.nero.com
FAAD2 使用 GPL-2.0-or-later；包含该核心的插件整体分发须遵循其条款，自有代码的 MIT 许可不替代第三方许可。

[本次构建的源码、许可证和构建说明]($repoUrl/tree/$Commit)
[FAAD2 完整许可证]($repoUrl/blob/$Commit/third_party/faad2/COPYING)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8
if ($env:GITHUB_OUTPUT) {
    "version=$version" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
Write-Output "Prepared AAC release $version"
