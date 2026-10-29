# Starts the real game from the trace copy and screenshots the window every $Step ms for $Seconds s.
# Keys given as "seconds:vk" (hex vk) are pressed at that time. Output: $OutDir\t<ms>.png
param([string]$OutDir = "D:\Projects\C++\OpenBGI\analysis\out\real_timeline", [int]$Seconds = 60, [int]$Step = 700,
      [string[]]$Keys = @("32:28", "34:0D"))
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W3 { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint fl, UIntPtr ex);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; } }
"@
[W3]::SetProcessDPIAware() | Out-Null
$Keys = @($Keys | ForEach-Object { $_ -split "," })
Remove-Item "$OutDir\t*.png" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null
$proc = Start-Process "E:\Games\_ac2_trace\ac2_ML.exe" -WorkingDirectory "E:\Games\_ac2_trace" -PassThru
$sw = [Diagnostics.Stopwatch]::StartNew()
$pressed = @{}
$focused = $false
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
  $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
  if (-not $p) { break }
  $p.Refresh()
  if ($p.MainWindowHandle -ne 0) {
    # the game only presents while it is the foreground window
    [W3]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    $r = New-Object W3+RECT; [W3]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $ms = [int]$sw.Elapsed.TotalMilliseconds
    $bmp.Save(("{0}\t{1:D6}.png" -f $OutDir, $ms), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
  }
  foreach ($k in $Keys) {
    $t, $vk = $k.Split(":")
    if (-not $pressed.ContainsKey($k) -and $sw.Elapsed.TotalSeconds -ge [double]$t) {
      $pressed[$k] = 1
      $v = [Convert]::ToByte($vk, 16)
      [W3]::keybd_event($v, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [W3]::keybd_event($v, 0, 2, [UIntPtr]::Zero)
    }
  }
  Start-Sleep -Milliseconds $Step
}
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
"done"
