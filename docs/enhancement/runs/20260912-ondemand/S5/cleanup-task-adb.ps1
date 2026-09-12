$ErrorActionPreference = 'Stop'
$taskMetaPath = Join-Path $PSScriptRoot 'wipe-adb-isolated-server.json'
$taskMeta = Get-Content -LiteralPath $taskMetaPath -Raw | ConvertFrom-Json
if ($taskMeta.port -ne 5038 -or $taskMeta.originalServerPort -ne 5037) {
    throw 'Unexpected task ADB ports'
}
$taskListener = Get-NetTCPConnection -LocalPort $taskMeta.port -State Listen
$taskProcess = Get-Process -Id $taskMeta.pid
if ($taskListener.OwningProcess -ne $taskMeta.pid -or
    $taskProcess.StartTime.ToUniversalTime().Ticks -ne ([DateTime]$taskMeta.serverStartUtc).ToUniversalTime().Ticks -or
    $taskProcess.Path -ne $taskMeta.executablePath) {
    throw 'Task server ownership changed; refusing shutdown'
}
& $taskMeta.executablePath -P $taskMeta.port kill-server
if ($LASTEXITCODE -ne 0) { throw 'Task ADB shutdown failed' }
if (Get-NetTCPConnection -LocalPort $taskMeta.port -State Listen -ErrorAction SilentlyContinue) {
    throw 'Task ADB port still listening'
}
$originalProcess = Get-Process -Id $taskMeta.originalServerPid
$taskMeta.cleanupPending = $false
$taskMeta | Add-Member -NotePropertyName stoppedAtUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$taskMeta | Add-Member -NotePropertyName originalServerAfterCleanup -NotePropertyValue @{
    pid = $originalProcess.Id
    startUtc = $originalProcess.StartTime.ToUniversalTime().ToString('o')
    path = $originalProcess.Path
}
$taskMeta | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskMetaPath -Encoding utf8
Write-Output "Task ADB 5038 stopped; original server PID $($originalProcess.Id) retained"
