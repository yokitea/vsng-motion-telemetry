$process = Get-Process -Name vsf_ng -ErrorAction SilentlyContinue
if (-not $process) {
    Write-Host "vsf_ng process not found!"
    exit
}

Write-Host "Checking modules for process $($process.Name) (PID: $($process.Id))..."
$modules = $process | Select-Object -ExpandProperty Modules
$matching = $modules | Where-Object { $_.ModuleName -like "*instrument*" -or $_.ModuleName -like "*telemetry*" -or $_.FileName -like "*instrument*" -or $_.FileName -like "*telemetry*" }

if ($matching) {
    Write-Host "Found matching modules:"
    $matching | Select-Object ModuleName, FileName | Format-Table -AutoSize
} else {
    Write-Host "No matching modules (instrument or telemetry) are loaded in vsf_ng."
}
