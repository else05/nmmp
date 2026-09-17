[CmdletBinding()]
param(
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot '..\apkprotect\src\main\resources\vmsrc.zip'
}

$vmSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\nmmvm\nmmvm\src\main\cpp'))
$output = [IO.Path]::GetFullPath($OutputPath)
$stage = Join-Path ([IO.Path]::GetTempPath()) ('nmmp-vmsrc-' + [guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Path $stage | Out-Null

    foreach ($name in @('vm', 'cutils', 'ConstantPool.c', 'ConstantPool.h')) {
        Copy-Item -LiteralPath (Join-Path $vmSource $name) -Destination $stage -Recurse
    }

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vm\CMakeLists.txt') `
        -Destination (Join-Path $stage 'vm\CMakeLists.txt') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Destination $stage

    $loaderStage = Join-Path $stage 'loader'
    New-Item -ItemType Directory -Path $loaderStage | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'loader') -Force |
        Where-Object { $_.Name -notin @('tests', '__pycache__') } |
        Copy-Item -Destination $loaderStage -Recurse

    $outputDirectory = Split-Path -Parent $output
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $output) {
        Remove-Item -LiteralPath $output -Force
    }

    $jarCommand = if ($env:JAVA_HOME) {
        Join-Path $env:JAVA_HOME 'bin\jar.exe'
    } else {
        (Get-Command jar.exe -ErrorAction Stop).Source
    }
    if (-not (Test-Path -LiteralPath $jarCommand)) {
        throw "jar.exe not found: $jarCommand"
    }

    & $jarCommand --create --file $output --no-compress --no-manifest -C $stage .
    if ($LASTEXITCODE -ne 0) {
        throw "jar.exe failed with exit code $LASTEXITCODE"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($output)
    try {
        $names = @($zip.Entries.FullName)
        foreach ($required in @('CMakeLists.txt', 'vm/CMakeLists.txt', 'loader/Loader.c')) {
            if ($required -notin $names) {
                throw "Generated vmsrc.zip is missing $required"
            }
        }
        if ($names -match '^loader/(tests|__pycache__)/') {
            throw 'Generated vmsrc.zip contains excluded loader test/cache files'
        }
    } finally {
        $zip.Dispose()
    }

    $hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    Write-Host "vmsrc.zip: $output"
    Write-Host "SHA-256:   $hash"
} finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
