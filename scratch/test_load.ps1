$Signature = @"
[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr LoadLibrary(string lpFileName);
"@
$Kernel32 = Add-Type -MemberDefinition $Signature -Name "Kernel32" -Namespace "Win32" -PassThru
$h = $Kernel32::LoadLibrary("E:\Games\Virtual.Sailor.NG\instruments\vsng_telemetry\instrument64.dll")
if ($h -eq 0) {
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    $msg = (New-Object System.ComponentModel.Win32Exception($err)).Message
    Write-Host "Error code: $err - $msg"
} else {
    Write-Host "Loaded successfully! Handle: $h"
}
