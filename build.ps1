[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$Generator = 'Visual Studio 18 2026',
    [string[]]$CMakeArguments = @(),
    [switch]$Package,
    [switch]$SourcePackage,
    [string]$PackageVersion
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'cmake/version.ps1')
# Capture once before compilation, including the Actions same-day patch suffix.
$PackageVersion = (Get-AacBuildVersion $PackageVersion).Name
function Invoke-CMake([string[]]$Arguments) {
    # Windows PowerShell 5 treats redirected native stderr as ErrorRecord even
    # for CMake's successful status messages. The process exit code is decisive.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & cmake @Arguments 2>&1 | ForEach-Object { $_.ToString() }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "CMake failed ($code): $Arguments" }
}
Invoke-CMake (@('-S',$PSScriptRoot,'-B',$BuildDirectory,'-G',$Generator,'-A','Win32') + $CMakeArguments +
    @("-DTTP_AAC_BUILD_VERSION=$PackageVersion"))
Invoke-CMake @('--build',$BuildDirectory,'--config','Release','--target','ttp_aac','--parallel','4')
Assert-AacFileVersion (Join-Path $BuildDirectory 'Release/ttp_aac.dll') $PackageVersion
if (-not ($Package -or $SourcePackage)) { return }

$output = Join-Path $BuildDirectory 'Release'
$stage = Join-Path $BuildDirectory ('package-' + [guid]::NewGuid().ToString('N'))
$binary = Join-Path $stage 'binary'
New-Item -ItemType Directory -Path $binary -Force | Out-Null
# Explicit component and fresh staging keep licenses, docs and stale build files
# out of the runtime ZIP. All license texts remain in the source repository.
Invoke-CMake @('--install',$BuildDirectory,'--config','Release','--component','Runtime','--prefix',$binary)
$hash = (Get-FileHash -LiteralPath (Join-Path $binary 'AddIn/ttp_aac.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  AddIn/ttp_aac.dll" | Set-Content -LiteralPath (Join-Path $binary 'SHA256SUMS.txt') -Encoding UTF8
Compress-Archive -LiteralPath (Join-Path $binary 'AddIn'),(Join-Path $binary 'SHA256SUMS.txt') `
    -DestinationPath (Join-Path $output "ttp_aac-$PackageVersion.zip") -Force
$archives = @((Join-Path $output "ttp_aac-$PackageVersion.zip"))
if ($SourcePackage) {
    $source = Join-Path $stage 'source/ttp_aac'
    New-Item -ItemType Directory -Path $source -Force | Out-Null
    function Copy-AacSourceEntry([string]$Path, [string]$Destination) {
        $item = Get-Item -LiteralPath $Path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { return }
        if ($item.PSIsContainer) {
            if ($item.Name -in @('.git','__pycache__','WinRel','WinDebug')) { return }
            $target = Join-Path $Destination $item.Name
            New-Item -ItemType Directory -Path $target -Force | Out-Null
            foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) {
                Copy-AacSourceEntry $child.FullName $target
            }
        } elseif ($item.Extension.ToLowerInvariant() -notin @(
            '.pyc','.pyo','.pdb','.obj','.exe','.dll','.lib','.o','.a','.ilk','.pch')) {
            Copy-Item -LiteralPath $item.FullName -Destination $Destination
        }
    }
    # Explicit allowlist: no local tests, samples, binaries, caches or evidence.
    foreach ($entry in @('CMakeLists.txt','build.ps1','README.md','LICENSE','.gitignore',
                         '.clang-format','cmake','src','include','docs','.github')) {
        Copy-AacSourceEntry (Join-Path $PSScriptRoot $entry) $source
    }
    # Preserve the resource version when rebuilding this source archive later.
    $PackageVersion | Set-Content -LiteralPath (Join-Path $source 'BUILD_VERSION') -Encoding ASCII
    $notices = Join-Path $source 'third_party/faad2'
    $decoderSource = Join-Path $notices 'source'
    New-Item -ItemType Directory -Path $decoderSource -Force | Out-Null
    foreach ($entry in @('COPYING','AUTHORS','ORIGIN.txt')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "third_party/faad2/$entry") -Destination $notices
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third_party/SSR_REFERENCE_NOTICE.txt') -Destination (Join-Path $source 'third_party')
    $upstream = (Get-Content -LiteralPath (Join-Path $BuildDirectory 'faad2-source-path.txt') -Encoding UTF8 -Raw).Trim()
    foreach ($entry in @('include','libfaad','COPYING','AUTHORS','README','properties.json')) {
        Copy-AacSourceEntry (Join-Path $upstream $entry) $decoderSource
    }
    $sourceArchive = Join-Path $output "ttp_aac-$PackageVersion-source.zip"
    Compress-Archive -LiteralPath $source -DestinationPath $sourceArchive -Force
    $archives += $sourceArchive
}
Get-FileHash -LiteralPath $archives -Algorithm SHA256 |
    ForEach-Object { '{0}  {1}' -f $_.Hash.ToLowerInvariant(),[IO.Path]::GetFileName($_.Path) } |
    Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding UTF8
Write-Output "AAC packages: $output"
