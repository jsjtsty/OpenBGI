# Launches the real game from the trace copy, screenshots the window at intervals; presses Down+Enter on the title.
param([string]$OutDir = "D:\Projects\C++\OpenBGI\analysis\out\real_shots", [string[]]$Keys = @("28","0D"))
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W2 { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint fl, UIntPtr ex);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; } }
"@
[W2]::SetProcessDPIAware() | Out-Null
function Shot($name) {
  $p = Get-Process ac2_ML -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $p) { "no window"; return }
  [W2]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 400
  $r = New-Object W2+RECT; [W2]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save("$OutDir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png); "saved $name $w x $h"
}
function Press($vk) { $v=[Convert]::ToByte($vk,16); [W2]::keybd_event($v,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [W2]::keybd_event($v,0,2,[UIntPtr]::Zero) }
$proc = Start-Process "E:\Games\_ac2_trace\ac2_ML.exe" -WorkingDirectory "E:\Games\_ac2_trace" -PassThru
Start-Sleep 30
Shot "title"
foreach ($k in $Keys) { Press $k; Start-Sleep 2; Shot "after_$k" }
foreach ($t in 3, 5, 10) { Start-Sleep $t; Shot "later_$t" }
Stop-Process -Id $proc.Id -Force
