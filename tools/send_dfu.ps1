# Send raw HID CMD_BOOTLOADER (0x14) to bstmouse QMK firmware via Win32 API
$src = @"
using System;
using System.Runtime.InteropServices;
public class FHid {
  [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
  public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool WriteFile(IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToWrite, out uint lpNumberOfBytesWritten, IntPtr lpOverlapped);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool CloseHandle(IntPtr hObject);
}
"@
Add-Type -TypeDefinition $src

# discover the raw HID interface (MI_01, vendor-defined) dynamically
$dev = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'HID\\VID_16C0&PID_047E&MI_01\\' } | Select-Object -First 1
if (-not $dev) { Write-Host "ERR: raw HID interface not found"; exit 1 }
$inst = ($dev.InstanceId -split '\\')[2]   # e.g. 7&2B00DA4D&0&0000
$path = "\\?\hid#vid_16c0&pid_047e&mi_01#" + $inst.ToLower() + "#{4d1e55b2-f16f-11cf-88cb-001111000030}"
Write-Host "opening: $path"

$h = [FHid]::CreateFile($path, [uint32]3221225472, [uint32]3, [IntPtr]::Zero, [uint32]3, [uint32]0, [IntPtr]::Zero)
if ($h.ToInt64() -eq -1) {
  Write-Host ("ERR: CreateFile failed, GLE=" + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
  exit 1
}
$buf = New-Object byte[] 33
$buf[0] = 0      # report ID: none
$buf[1] = 0x14   # CMD_BOOTLOADER
$written = 0
$ok = [FHid]::WriteFile($h, $buf, 33, [ref]$written, [IntPtr]::Zero)
[FHid]::CloseHandle($h) | Out-Null
if ($ok) { Write-Host "OK: BOOTLOADER cmd sent ($written bytes)" } else {
  Write-Host ("ERR: WriteFile failed, GLE=" + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
  exit 1
}
