[CmdletBinding()]
param(
    [string]$EnvFile,
    [string]$ProjectRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Import-EnvFile([string]$Path) {
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith('#')) { continue }
        $separator = $text.IndexOf('=')
        if ($separator -lt 1) { throw "Invalid env line: $line" }
        $name = $text.Substring(0, $separator).Trim()
        $value = $text.Substring($separator + 1).Trim()
        if ($name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw "Invalid env name: $name" }
        if ($value.Length -eq 0) {
            Remove-Item -Path "Env:$name" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

$envBase = (Get-Location).Path
if (-not $EnvFile) {
    $defaultEnv = Join-Path $PSScriptRoot 'nmmp.env'
    if (Test-Path -LiteralPath $defaultEnv) { $EnvFile = $defaultEnv }
}
if ($EnvFile) {
    $EnvFile = (Resolve-Path -LiteralPath $EnvFile).Path
    $envBase = Split-Path -Parent $EnvFile
    Import-EnvFile $EnvFile
    Write-Host "Environment: $EnvFile"
}

if (-not $ProjectRoot) { $ProjectRoot = $env:NMMP_PROJECT_ROOT }
if (-not $ProjectRoot) { $ProjectRoot = 'E:\OtherProject\safe-toolchain\nmmp' }
if (-not [IO.Path]::IsPathRooted($ProjectRoot)) { $ProjectRoot = Join-Path $envBase $ProjectRoot }
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path

if ($env:JAVA_HOME) {
    $javaBin = Join-Path $env:JAVA_HOME 'bin'
    if (-not (Test-Path -LiteralPath (Join-Path $javaBin 'java.exe'))) {
        throw "JAVA_HOME does not contain java.exe: $env:JAVA_HOME"
    }
    $env:Path = "$javaBin;$env:Path"
}

$protectRoot = Join-Path $ProjectRoot 'nmm-protect'
$gradle = Join-Path $protectRoot 'gradlew.bat'
$sourceBuilder = Join-Path $protectRoot 'mksrc\build-src.ps1'
if (-not (Test-Path -LiteralPath $gradle)) { throw "Gradle wrapper not found: $gradle" }
if (-not (Test-Path -LiteralPath $sourceBuilder)) { throw "Source packer not found: $sourceBuilder" }

& $sourceBuilder
if ($LASTEXITCODE -ne 0) { throw "vmsrc.zip generation failed with exit code $LASTEXITCODE" }

$workers = if ($env:CMAKE_BUILD_PARALLEL_LEVEL -match '^[1-9][0-9]*$') {
    [int]$env:CMAKE_BUILD_PARALLEL_LEVEL
} else {
    [Math]::Max(1, [Environment]::ProcessorCount - 3)
}

$started = Get-Date
Push-Location $protectRoot
try {
    & $gradle :apkprotect:test jar "--max-workers=$workers" --console=plain
    if ($LASTEXITCODE -ne 0) { throw "Gradle failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$jar = Get-ChildItem -LiteralPath (Join-Path $protectRoot 'build\libs') -Filter 'vm-protect-*.jar' |
    Where-Object { $_.LastWriteTime -ge $started.AddMinutes(-1) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $jar) { throw 'The current build did not produce a vm-protect JAR' }

$sourceZip = Join-Path $protectRoot 'apkprotect\src\main\resources\vmsrc.zip'
Write-Host "Built JAR: $($jar.FullName)"
Write-Host "JAR SHA-256: $((Get-FileHash -LiteralPath $jar.FullName -Algorithm SHA256).Hash)"

if ($env:NMMP_DEPLOY_JAR) {
    $deployValue = $env:NMMP_DEPLOY_JAR
    if (-not [IO.Path]::IsPathRooted($deployValue)) { $deployValue = Join-Path $envBase $deployValue }
    $deployJar = [IO.Path]::GetFullPath($deployValue)
    $deployDirectory = Split-Path -Parent $deployJar
    $deployTools = Join-Path $deployDirectory 'tools'
    New-Item -ItemType Directory -Path $deployDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $deployTools -Force | Out-Null
    Copy-Item -LiteralPath $jar.FullName -Destination $deployJar -Force
    Copy-Item -LiteralPath $sourceZip -Destination (Join-Path $deployTools 'vmsrc.zip') -Force

    $sourceJarHash = (Get-FileHash -LiteralPath $jar.FullName -Algorithm SHA256).Hash
    $deployJarHash = (Get-FileHash -LiteralPath $deployJar -Algorithm SHA256).Hash
    $sourceZipHash = (Get-FileHash -LiteralPath $sourceZip -Algorithm SHA256).Hash
    $deployZipHash = (Get-FileHash -LiteralPath (Join-Path $deployTools 'vmsrc.zip') -Algorithm SHA256).Hash
    if ($sourceJarHash -ne $deployJarHash -or $sourceZipHash -ne $deployZipHash) {
        throw 'Deployment hash verification failed'
    }
    Write-Host "Deployed JAR: $deployJar"
    Write-Host "Deployed vmsrc.zip: $(Join-Path $deployTools 'vmsrc.zip')"
}
