param(
    [Parameter(Mandatory = $true)][string]$Serial,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build/detection-android')
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$fixtures = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../mksrc/loader/tests')).Path
$remote = '/data/local/tmp/nmmp-detection-tests'
function Invoke-AdbChecked {
    param([string[]]$Arguments)
    $output = & adb -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $output" }
    return ($output -join "`n")
}
$executables = @('policy_decision_test', 'policy_runtime_test', 'process_maps_test',
    'image_registry_test', 'native_integrity_test', 'environment_checks_test', 'artifact_inventory_test', 'artifact_apk_test', 'artifact_signature_test', 'loader_test', 'crypto_test', 'once_test', 'module_test')
$files = $executables + @('libtest_inner.so', 'module.content', 'artifact-inventory.bin',
    'stored.apk', 'deflated.apk', 'changed.apk', 'extra.apk', 'missing.apk', 'large.apk', 'large-inventory.bin', 'artifact-signed.bin', 'artifact-public.bin',
    'signed-valid.apk', 'signed-aligned.apk', 'signed-changed.apk', 'signed-invalid.apk', 'signed-compressed.apk')
foreach ($name in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $name) -PathType Leaf)) { throw "Missing build output: $name" }
}
Invoke-AdbChecked @('shell', 'mkdir', '-p', $remote) | Out-Null
foreach ($name in $files) {
    Invoke-AdbChecked @('push', (Join-Path $build $name), "$remote/$name") | Out-Null
}
foreach ($name in @('content.golden', 'envelope.golden')) {
    Invoke-AdbChecked @('push', (Join-Path $fixtures $name), "$remote/$name") | Out-Null
}
foreach ($name in $executables) { Invoke-AdbChecked @('shell', 'chmod', '700', "$remote/$name") | Out-Null }
$cases = [System.Collections.Generic.List[object]]::new()
foreach ($name in @('policy_decision_test', 'process_maps_test', 'image_registry_test', 'native_integrity_test', 'environment_checks_test', 'once_test')) {
    $cases.Add(@{name=$name; arguments=@("$remote/$name")})
}
foreach ($case in @('truncation', 'integrity-race', 'interval', 'single-sampler', 'maps-tail',
        'maps-invalid', 'jni-pending', 'jni-method-error', 'jni-call-error', 'app-debug-retained', 'native-mismatch', 'native-unavailable', 'environment-startup',
        'clock-startup', 'clock-periodic', 'clock-acquired', 'clock-completion', 'art-diagnostic', 'art-debug-preserved')) {
    $cases.Add(@{name="policy_$case"; arguments=@("$remote/policy_runtime_test", $case)})
}
foreach ($case in @('reentry-waiters', 'failure-before-init', 'failure-after-ready')) {
    $cases.Add(@{name="once_$case"; arguments=@("$remote/once_test", $case)})
}
$cases.Add(@{name='loader_content'; arguments=@("$remote/loader_test", "$remote/content.golden")})
$cases.Add(@{name='crypto_envelope'; arguments=@("$remote/crypto_test", "$remote/envelope.golden")})
$cases.Add(@{name='module'; arguments=@("$remote/module_test", "$remote/libtest_inner.so", "$remote/module.content")})
$cases.Add(@{name='artifact_inventory'; arguments=@("$remote/artifact_inventory_test", "$remote/artifact-inventory.bin")})
$cases.Add(@{name='artifact_apk'; arguments=@("$remote/artifact_apk_test", $remote)})
$cases.Add(@{name='artifact_signature'; arguments=@("$remote/artifact_signature_test", $remote)})
$report = [ordered]@{
    serial=$Serial
    api=(Invoke-AdbChecked @('shell', 'getprop', 'ro.build.version.sdk')).Trim()
    abi=(Invoke-AdbChecked @('shell', 'getprop', 'ro.product.cpu.abi')).Trim()
    time=(Get-Date).ToUniversalTime().ToString('o')
    binaries=@{}
    results=[System.Collections.Generic.List[object]]::new()
}
foreach ($name in $files) { $report.binaries[$name] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $build $name)).Hash }
$reportPath = Join-Path $build ('device-results-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
foreach ($case in $cases) {
    try {
        $output = Invoke-AdbChecked (@('shell') + $case.arguments)
        $report.results.Add(@{name=$case.name; passed=$true; output=$output})
        Write-Output "PASS $($case.name)"
    } catch {
        $report.results.Add(@{name=$case.name; passed=$false; output=$_.Exception.Message})
        throw
    } finally {
        $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    }
}
Write-Output "Passed $($cases.Count) device tests. Report: $reportPath"
