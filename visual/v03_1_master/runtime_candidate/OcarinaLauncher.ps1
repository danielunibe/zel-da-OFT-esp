Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Management

# --- Configuration ---
$baseDir = $PSScriptRoot
$jsonPath = Join-Path $baseDir "shipofharkinian.json"
$exePath = Join-Path $baseDir "soh.exe"
$modsPath = Join-Path $baseDir "mods"
$bgPath = Join-Path $baseDir "background.png"
$iconPath = Join-Path $baseDir "xbox_icon.png"
$naviPath = Join-Path $baseDir "NaviAssistant.ps1"

# --- Backend Logic ---
function Get-Config {
    if (Test-Path $jsonPath) { return Get-Content $jsonPath -Raw | ConvertFrom-Json }
    return $null
}

function Save-Config ($config) {
    if ($config) { $config | ConvertTo-Json -Depth 10 | Set-Content $jsonPath }
}

function Find-Controller {
    return Get-CimInstance Win32_PnPEntity | Where-Object { $_.Description -match "Xbox|Controller|Gamepad" -or $_.Name -match "Xbox|XInput" } | Select-Object -First 1
}

function Optimize-System {
    try {
        $gpu = Get-CimInstance Win32_VideoController
        $isHighEnd = $gpu.Name -match "RTX|GTX 1|Radeon RX 6|Radeon RX 7|Titan|Arc"
        
        $conf = Get-Config
        if (-not $conf) { return $false }

        if (-not $conf.CVars) { $conf | Add-Member -NotePropertyName "CVars" -NotePropertyValue @{} }
        $conf.CVars | Add-Member -NotePropertyName "gMatchRefreshRate" -NotePropertyValue 1 -Force
        $conf.CVars | Add-Member -NotePropertyName "gInterpolationFPS" -NotePropertyValue 1 -Force
        $conf.CVars | Add-Member -NotePropertyName "gDisableLOD" -NotePropertyValue 1 -Force
        
        if ($isHighEnd) {
            $conf.CVars | Add-Member -NotePropertyName "gMSAA" -NotePropertyValue 8 -Force
            $conf.CVars | Add-Member -NotePropertyName "gExtra4K" -NotePropertyValue 1 -Force
        }
        else {
            $conf.CVars | Add-Member -NotePropertyName "gMSAA" -NotePropertyValue 2 -Force
        }
        Save-Config $conf
        return $true
    }
    catch { return $false }
}

# --- State ---
$global:currentScreen = "HOME"
$global:xboxDetected = $false
$global:particles = [System.Collections.ArrayList]::new()
$global:buttons = [System.Collections.ArrayList]::new()

# --- Double Buffered Form ---
$formCode = @"
using System.Windows.Forms;
public class DoubleBufferedForm : Form {
    public DoubleBufferedForm() {
        this.DoubleBuffered = true;
        this.SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);
        this.UpdateStyles();
    }
}
"@
Add-Type -TypeDefinition $formCode -ReferencedAssemblies System.Windows.Forms

$form = New-Object DoubleBufferedForm
$form.Text = "Hyrule Gateway - Daniel Unibe Edition"
$form.Size = New-Object System.Drawing.Size(900, 600)
$form.FormBorderStyle = "None"
$form.StartPosition = "CenterScreen"
$form.BackColor = [System.Drawing.Color]::FromArgb(10, 10, 20)

# --- Load Images ---
$bgImage = $null; $xboxIcon = $null
try { if (Test-Path $bgPath) { $bgImage = [System.Drawing.Image]::FromFile($bgPath) } } catch {}
try { if (Test-Path $iconPath) { $xboxIcon = [System.Drawing.Image]::FromFile($iconPath) } } catch {}

# --- Colors ---
$darkOverlay = [System.Drawing.Color]::FromArgb(180, 0, 0, 0)
$titleFont = New-Object System.Drawing.Font("Segoe UI", 28, [System.Drawing.FontStyle]::Bold)
$subFont = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$btnFont = New-Object System.Drawing.Font("Segoe UI", 12, [System.Drawing.FontStyle]::Bold)

# --- Particle System ---
$rng = New-Object Random
for ($i = 0; $i -lt 30; $i++) {
    $null = $global:particles.Add(@{
            X = $rng.Next(0, 900); Y = $rng.Next(0, 600)
            SpeedY = - ($rng.NextDouble() * 1 + 0.3)
            Size = $rng.Next(2, 5)
            Alpha = $rng.Next(50, 200)
            Color = @([System.Drawing.Color]::Cyan, [System.Drawing.Color]::White, [System.Drawing.Color]::Gold)[$rng.Next(0, 3)]
        })
}

# --- Button Factory ---
function Add-Btn($x, $y, $w, $h, $txt, $action, $tag) {
    $null = $global:buttons.Add(@{
            Rect = New-Object System.Drawing.Rectangle($x, $y, $w, $h)
            Text = $txt; Action = $action; Hovering = $false; Tag = $tag
        })
}

function Check-Translation {
    $otrFiles = Get-ChildItem $modsPath -Filter "*.otr"
    if ($otrFiles.Count -gt 0) { return $true }
    return $false
}

function Build-HomeUI {
    $global:buttons.Clear()
    Add-Btn 850 10 40 40 "X" { $form.Close() } "EXIT"
    
    # Translation Status
    $hasLang = Check-Translation
    if ($hasLang) {
        Add-Btn 50 130 220 40 "IDIOMA: DETECTADO" { [System.Windows.Forms.MessageBox]::Show("Archivos de mod detectados. Ve a Settings -> Languages en el juego (F1).", "Info") } "LANG_OK"
    }
    else {
        Add-Btn 50 130 220 40 "⚠️ INSTALAR ESPAÑOL" { 
            $downPath = [System.IO.Path]::Combine($env:USERPROFILE, "Downloads")
            [System.Windows.Forms.MessageBox]::Show("MODO MAGIA ACTIVADO 🪄`n`n1. Voy a abrir la página de descarga.`n2. TÚ solo descarga el archivo (zip o otr).`n3. YO lo detectaré y lo instalaré solo.`n`n¡No cierres esta ventana!", "Asistente AI")
            
            # Open Guide/Download Search
            Start-Process "https://www.youtube.com/results?search_query=ship+of+harkinian+espa%C3%B1ol+download+link"
            
            # Start Watcher/Scanner in Background Job to avoid freezing UI (simulated with standard loop for stability in single thread)
            $found = $false
            $maxRetries = 60 # 60 seconds wait
            
            # Check existing first
            $existing = Get-ChildItem -Path $downPath -Filter "*spanish*.otr" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($existing) {
                Move-Item $existing.FullName -Destination $modsPath -Force
                $found = $true
            }

            if (-not $found) {
                # Simple polling for 60 seconds 
                for ($i = 0; $i -lt 60; $i++) {
                    [System.Windows.Forms.Application]::DoEvents()
                    Start-Sleep -Milliseconds 1000
                    $newFile = Get-ChildItem -Path $downPath -Filter "*.otr" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-2) } | Select-Object -First 1
                    
                    if ($newFile) {
                        try {
                            Move-Item $newFile.FullName -Destination $modsPath -Force
                            $found = $true
                            break
                        }
                        catch {
                            # File might be locked (still downloading)
                            Write-Host "File locked, waiting..."
                        }
                    }
                }
            }
            
            if ($found) {
                [System.Windows.Forms.MessageBox]::Show("¡ARCHIVO CAPTURADO! 🦅`n`nHe movido el mod a la carpeta correcta.`nReinicia el launcher para ver el cambio.", "Éxito")
                $form.Close()
            }
            else {
                [System.Windows.Forms.MessageBox]::Show("No detecté la descarga a tiempo.`n`nAsegúrate de descargar un archivo .otr y vuelve a intentarlo.", "Tiempo agotado")
            }

        } "LANG_MISSING"
    }

    Add-Btn 550 350 280 50 "INVOCAR NAVI (IA)" { Start-Process powershell -ArgumentList "-ExecutionPolicy Bypass -File `"$naviPath`"" } "NAVI"
    Add-Btn 550 420 280 70 "JUGAR" { Start-Process $exePath; $form.Close() } "PLAY"
    Add-Btn 50 200 220 50 "GESTOR DE MODS" { $global:currentScreen = "MODS"; Build-ModsUI } "MODS"
    
    Add-Btn 50 270 220 50 "OPTIMIZAR PC" {
        Optimize-System | Out-Null
        $ctrl = Find-Controller
        $global:xboxDetected = ($null -ne $ctrl)
        
        # Force Language Config
        $conf = Get-Config
        if ($conf) {
            if (-not $conf.CVars) { $conf | Add-Member -NotePropertyName "CVars" -NotePropertyValue @{} }
            $conf.CVars.gLanguages = 1
            $conf.CVars.gLanguage = "Spanish"
            Save-Config $conf
        }

        [System.Windows.Forms.MessageBox]::Show("Sistema Optimizado y Configuración de Idioma Forzada.`nControl: $(if($global:xboxDetected){'Detectado'}else{'No detectado'})", "Sheikah Slate")
    } "OPT"
}

function Build-ModsUI {
    $global:buttons.Clear()
    Add-Btn 850 10 40 40 "X" { $form.Close() } "EXIT"
    Add-Btn 50 520 150 50 "< VOLVER" { $global:currentScreen = "HOME"; Build-HomeUI } "BACK"
    Add-Btn 650 520 200 50 "ABRIR CARPETA" { Invoke-Item $modsPath } "OPEN"
    
    $y = 100
    if (Test-Path $modsPath) {
        Get-ChildItem $modsPath -File | ForEach-Object {
            if ($y -lt 480) {
                $mName = $_.Name
                $isDis = $mName -like "*.disabled"
                $cleanName = if ($isDis) { $mName -replace "\.disabled", "" } else { $mName }
                $status = if ($isDis) { "[OFF]" } else { "[ON]" }
                
                $toggleAction = [scriptblock]::Create("
                    `$src = Join-Path '$modsPath' '$mName'
                    if ('$isDis' -eq 'True') { Rename-Item `$src -NewName ('$mName' -replace '\.disabled','') }
                    else { Rename-Item `$src -NewName '$mName.disabled' }
                    Build-ModsUI
                ")
                Add-Btn 50 $y 600 40 "$status $cleanName" $toggleAction "MOD"
                $y += 50
            }
        }
    }
}

Build-HomeUI

# --- Dragging ---
$script:isDragging = $false
$script:dragStart = [System.Drawing.Point]::Empty

# --- Timer (Particles) ---
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 50
$timer.Add_Tick({
        foreach ($p in $global:particles) {
            $p.Y += $p.SpeedY
            if ($p.Y -lt -10) { $p.Y = 610; $p.X = $rng.Next(0, 900) }
        }
        $form.Invalidate()
    })
$timer.Start()

# --- Paint ---
$form.Add_Paint({
        param($s, $e)
        $g = $e.Graphics
        $g.SmoothingMode = "AntiAlias"
        $g.TextRenderingHint = "AntiAlias"

        # Background
        if ($bgImage) { $g.DrawImage($bgImage, 0, 0, 900, 600) }
        $brush = New-Object System.Drawing.SolidBrush($darkOverlay)
        $g.FillRectangle($brush, 0, 0, 900, 600)
        $brush.Dispose()

        # Particles
        foreach ($p in $global:particles) {
            $b = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($p.Alpha, $p.Color))
            $g.FillEllipse($b, [int]$p.X, [int]$p.Y, $p.Size, $p.Size)
            $b.Dispose()
        }

        # Title
        if ($global:currentScreen -eq "HOME") {
            $g.DrawString("OCARINA OF TIME", $titleFont, [System.Drawing.Brushes]::Cyan, 50, 40)
            $g.DrawString("Daniel Unibe Edition", $subFont, [System.Drawing.Brushes]::Gold, 55, 90)
        
            if ($global:xboxDetected -and $xboxIcon) {
                $g.DrawImage($xboxIcon, 50, 340, 40, 40)
                $g.DrawString("Control Xbox Detectado", $btnFont, [System.Drawing.Brushes]::LimeGreen, 100, 350)
            }
        }
        elseif ($global:currentScreen -eq "MODS") {
            $g.DrawString("GESTOR DE MODS", $titleFont, [System.Drawing.Brushes]::Gold, 50, 30)
        }

        # Buttons
        foreach ($b in $global:buttons) {
            $bgColor = if ($b.Hovering) { [System.Drawing.Color]::FromArgb(80, 0, 255, 255) } else { [System.Drawing.Color]::FromArgb(50, 255, 255, 255) }
            $btnBrush = New-Object System.Drawing.SolidBrush($bgColor)
            $g.FillRectangle($btnBrush, $b.Rect)
            $btnBrush.Dispose()
        
            $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::Cyan, 1)
            $g.DrawRectangle($pen, $b.Rect)
            $pen.Dispose()
        
            $sf = New-Object System.Drawing.StringFormat
            $sf.Alignment = "Center"; $sf.LineAlignment = "Center"
        
            # FIX: Explicit RectangleF to ensure correct overload
            $rectF = New-Object System.Drawing.RectangleF($b.Rect.X, $b.Rect.Y, $b.Rect.Width, $b.Rect.Height)
            $g.DrawString($b.Text, $btnFont, [System.Drawing.Brushes]::White, $rectF, $sf)
        }
        # Border
        $borderPen = New-Object System.Drawing.Pen([System.Drawing.Color]::Cyan, 2)
        $g.DrawRectangle($borderPen, 0, 0, 898, 598)
        $borderPen.Dispose()
    })

# --- Mouse Events ---
$form.Add_MouseMove({
        param($s, $e)
        if ($script:isDragging) {
            $current = [System.Windows.Forms.Cursor]::Position
            $form.Location = New-Object System.Drawing.Point(($current.X - $script:dragStart.X), ($current.Y - $script:dragStart.Y))
        }
        foreach ($b in $global:buttons) {
            $b.Hovering = $b.Rect.Contains($e.Location)
        }
    })

$form.Add_MouseDown({
        param($s, $e)
        $clicked = $false
        foreach ($b in $global:buttons) {
            if ($b.Rect.Contains($e.Location)) {
                & $b.Action
                $clicked = $true
                break
            }
        }
        if (-not $clicked) {
            $script:isDragging = $true
            $script:dragStart = New-Object System.Drawing.Point($e.X, $e.Y)
        }
    })

$form.Add_MouseUp({ $script:isDragging = $false })

# --- Show ---
$form.ShowDialog()
$timer.Stop()
if ($bgImage) { $bgImage.Dispose() }
if ($xboxIcon) { $xboxIcon.Dispose() }
