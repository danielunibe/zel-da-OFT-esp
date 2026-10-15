[CmdletBinding()]
param([Parameter(Mandatory)][int]$ProcessId, [Parameter(Mandatory)][string]$RunRoot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class CouchMonitorCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    public delegate bool WindowCallback(IntPtr window, IntPtr argument);
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr argument);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool LogicalToPhysicalPointForPerMonitorDPI(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("dwmapi.dll")] public static extern int DwmFlush();
    public sealed class CaptureResult {
        public int X, Y, Width, Height, LogicalWidth, LogicalHeight, DwmResult;
        public uint Dpi;
        public bool ForegroundAfter;
    }
    public static IntPtr Find(int process) {
        IntPtr selected = IntPtr.Zero;
        long largest = 0;
        EnumWindows(delegate(IntPtr window, IntPtr unused) {
            uint owner; GetWindowThreadProcessId(window, out owner);
            Rect client;
            if (owner == process && IsWindowVisible(window) && GetClientRect(window, out client)) {
                long area = (long)(client.Right-client.Left)*(client.Bottom-client.Top);
                if (area > largest) { selected = window; largest = area; }
            }
            return true;
        }, IntPtr.Zero);
        return selected;
    }
    public static CaptureResult Capture(IntPtr window, string output) {
        // All DPI transitions and dependent API calls stay on this one native
        // thread. A PowerShell command boundary must not split this operation.
        IntPtr previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(window));
        if (previous == IntPtr.Zero) throw new InvalidOperationException("Cannot enter the game's DPI context.");
        Point origin = new Point();
        Point end = new Point();
        CaptureResult result = new CaptureResult();
        try {
            Rect client;
            if (!GetClientRect(window, out client)) throw new InvalidOperationException("Client rectangle unavailable.");
            result.LogicalWidth = client.Right-client.Left;
            result.LogicalHeight = client.Bottom-client.Top;
            if (result.LogicalWidth <= 0 || result.LogicalHeight <= 0) throw new InvalidOperationException("Empty client rectangle.");
            origin.X=client.Left; origin.Y=client.Top;
            end.X=client.Right; end.Y=client.Bottom;
            if (!ClientToScreen(window, ref origin) || !ClientToScreen(window, ref end) ||
                !LogicalToPhysicalPointForPerMonitorDPI(window, ref origin) ||
                !LogicalToPhysicalPointForPerMonitorDPI(window, ref end)) {
                throw new InvalidOperationException("Cannot convert both client corners to physical desktop pixels.");
            }
            result.Dpi=GetDpiForWindow(window);
        } finally { SetThreadDpiAwarenessContext(previous); }

        previous=SetThreadDpiAwarenessContext(new IntPtr(-4));
        if (previous == IntPtr.Zero) throw new InvalidOperationException("Cannot enter physical desktop DPI context.");
        try {
            result.X=origin.X; result.Y=origin.Y;
            result.Width=end.X-origin.X; result.Height=end.Y-origin.Y;
            int left=GetSystemMetrics(76), top=GetSystemMetrics(77);
            int width=GetSystemMetrics(78), height=GetSystemMetrics(79);
            if (result.Width <= 0 || result.Height <= 0 || origin.X < left || origin.Y < top ||
                end.X > left+width || end.Y > top+height) throw new InvalidOperationException("Client area is outside the physical desktop.");
            if (!IsWindowVisible(window) || GetForegroundWindow() != window) throw new InvalidOperationException("The test window is not visible in the foreground.");
            result.DwmResult=DwmFlush();
            if (result.DwmResult < 0) throw new InvalidOperationException("DWM did not complete its presentation flush.");
            using (Bitmap bitmap=new Bitmap(result.Width, result.Height)) {
                using (Graphics graphics=Graphics.FromImage(bitmap)) {
                    graphics.CopyFromScreen(origin.X, origin.Y, 0, 0, bitmap.Size, CopyPixelOperation.SourceCopy);
                }
                bitmap.Save(output, ImageFormat.Png);
            }
            result.ForegroundAfter=GetForegroundWindow() == window;
            return result;
        } finally { SetThreadDpiAwarenessContext(previous); }
    }
}
'@

$proofPath = Join-Path $RunRoot 'monitor.capture.json'
$releasePath = Join-Path $RunRoot 'monitor.capture.complete'
try {
    $presentPath = Join-Path $RunRoot 'presentation.evidence.jsonl'
    $deadline = (Get-Date).AddSeconds(10)
    $presentation = $null
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $presentPath) {
            $records = @(Get-Content -LiteralPath $presentPath | ForEach-Object { $_ | ConvertFrom-Json })
            $presentation = $records | Where-Object { $_.dxgi_present_call -eq 'ACCEPTED_BY_DXGI' } | Select-Object -Last 1
            if ($presentation) { break }
        }
        Start-Sleep -Milliseconds 50
    }
    if (-not $presentation) { throw 'No accepted Present record for the captured frame.' }
    $window = [CouchMonitorCapture]::Find($ProcessId)
    if ($window -eq [IntPtr]::Zero) { throw 'The test process has no visible client window.' }
    [CouchMonitorCapture]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 150
    if ([CouchMonitorCapture]::GetForegroundWindow() -ne $window) { throw 'The test window is not in the foreground.' }
    $output = Join-Path $RunRoot 'desktop_client_presented.png'
    $capture = [CouchMonitorCapture]::Capture($window, $output)
    $foregroundAfter = $capture.ForegroundAfter
    [ordered]@{
        status = if ($foregroundAfter) { 'CAPTURED_WINDOWS_DESKTOP_CLIENT' } else { 'FAIL_FOREGROUND_CHANGED' }
        processId = $ProcessId
        hwnd = $window.ToInt64()
        windowVisible = [CouchMonitorCapture]::IsWindowVisible($window)
        foregroundBefore = $true
        foregroundAfter = $foregroundAfter
        source = 'GDI_COPY_FROM_WINDOWS_DESKTOP_CLIENT_RECT_AFTER_DWM_FLUSH'
        physicalPanelMeasurement = 'NOT_AVAILABLE'
        frame = $presentation.forensic_frame
        present = $presentation.dxgi_present_call
        presentHresult = $presentation.hresult
        dwmFlushHresult = $capture.DwmResult
        clientRect = @{x=$capture.X; y=$capture.Y; width=$capture.Width; height=$capture.Height}
        logicalClientSize = @{width=$capture.LogicalWidth; height=$capture.LogicalHeight}
        windowDpi = $capture.Dpi
        coordinateSpace = 'PHYSICAL_DESKTOP_PIXELS_BOTH_CORNERS_CONVERTED_IN_ONE_NATIVE_CALL'
        capturedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        png = 'desktop_client_presented.png'
        sha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $proofPath -Encoding utf8
} catch {
    [ordered]@{ status='FAIL'; reason=$_.Exception.Message; processId=$ProcessId } |
        ConvertTo-Json | Set-Content -LiteralPath $proofPath -Encoding utf8
    Write-Warning $_.Exception.Message
} finally {
    Set-Content -LiteralPath $releasePath -Value 'SCREEN_CAPTURE_ATTEMPT_COMPLETE' -Encoding utf8
}
