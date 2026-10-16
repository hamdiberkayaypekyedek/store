# PKG Magaza - derleme oncesi hazirlik (Windows PowerShell)
# stb basliklarini indirir, fontlari kopyalar, right.sprx'i bulur ve ikonlari uretir.
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = $PSScriptRoot

New-Item -ItemType Directory -Force "$root\include", "$root\assets", "$root\sce_sys\about" | Out-Null

# 1) stb_image / stb_truetype (tek dosyalik, public domain)
foreach ($h in 'stb_image.h', 'stb_truetype.h') {
    $dst = "$root\include\$h"
    if (-not (Test-Path $dst)) {
        Invoke-WebRequest "https://raw.githubusercontent.com/nothings/stb/master/$h" -OutFile $dst -UseBasicParsing
        Write-Host "indirildi: include\$h"
    }
}

# 2) Fontlar (Turkce karakter destekli herhangi bir TTF olur; varsayilan Segoe UI - kisisel kullanim)
$fonts = [ordered]@{ 'font.ttf' = 'segoeui.ttf'; 'font-bold.ttf' = 'segoeuib.ttf' }
foreach ($name in $fonts.Keys) {
    $dst = "$root\assets\$name"
    if (-not (Test-Path $dst)) {
        $src = Join-Path $env:WINDIR "Fonts\$($fonts[$name])"
        if (Test-Path $src) { Copy-Item $src $dst; Write-Host "kopyalandi: assets\$name" }
        else { Write-Warning "$src yok; assets\$name dosyasini elle koyun (herhangi bir TTF)." }
    }
}

# 3) right.sprx (OpenOrbis orneklerinden)
$sprxDst = "$root\sce_sys\about\right.sprx"
if (-not (Test-Path $sprxDst)) {
    if ($env:OO_PS4_TOOLCHAIN -and (Test-Path $env:OO_PS4_TOOLCHAIN)) {
        $sprx = Get-ChildItem "$env:OO_PS4_TOOLCHAIN\samples" -Recurse -Filter right.sprx -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($sprx) { Copy-Item $sprx.FullName $sprxDst; Write-Host "kopyalandi: sce_sys\about\right.sprx" }
    }
    if (-not (Test-Path $sprxDst)) {
        Write-Warning "right.sprx bulunamadi. OpenOrbis toolchain'deki samples/*/sce_sys/about/right.sprx dosyasini sce_sys\about\ altina kopyalayin."
    }
}

# 4) XMB ikonu (512x512) ve arka plan (1920x1080)
Add-Type -AssemblyName System.Drawing
function Save-Art([string]$path, [int]$w, [int]$h, [bool]$logo) {
    $bmp = [System.Drawing.Bitmap]::new($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
    $c1 = [System.Drawing.Color]::FromArgb(0, 18, 56)
    $c2 = [System.Drawing.Color]::FromArgb(0, 72, 170)
    $brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $c1, $c2, [single]60)
    $g.FillRectangle($brush, $rect)
    if ($logo) {
        $s = [Math]::Min($w, $h)
        $bw = $s * 0.46; $bh = $s * 0.38
        $bx = ($w - $bw) / 2; $by = $h / 2 - $s * 0.10
        $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, [single]($s * 0.04))
        $g.DrawArc($pen, [single]($w / 2 - $s * 0.12), [single]($by - $s * 0.14), [single]($s * 0.24), [single]($s * 0.28), [single]180, [single]180)
        $g.FillRectangle([System.Drawing.Brushes]::White, [single]$bx, [single]$by, [single]$bw, [single]$bh)
        # Canta uzerinde korsan kafatasi + capraz kemikler
        $ink = [System.Drawing.Color]::FromArgb(0, 40, 110)
        $inkBrush = [System.Drawing.SolidBrush]::new($ink)
        $white = [System.Drawing.Brushes]::White
        $cx = $w / 2; $cy = $by + $bh * 0.45; $r = $s * 0.085
        $bone = [System.Drawing.Pen]::new($ink, [single]($s * 0.028))
        $bone.StartCap = 'Round'; $bone.EndCap = 'Round'
        $d = $s * 0.13
        $g.DrawLine($bone, [single]($cx - $d), [single]($cy - $d * 0.2), [single]($cx + $d), [single]($cy + $d * 1.3))
        $g.DrawLine($bone, [single]($cx + $d), [single]($cy - $d * 0.2), [single]($cx - $d), [single]($cy + $d * 1.3))
        foreach ($p in @(@(-1, -0.2), @(1, -0.2), @(-1, 1.3), @(1, 1.3))) {
            $ex = $cx + $p[0] * $d; $ey = $cy + $p[1] * $d; $er = $s * 0.018
            $g.FillEllipse($inkBrush, [single]($ex - $er * 2.2), [single]($ey - $er), [single]($er * 2), [single]($er * 2))
            $g.FillEllipse($inkBrush, [single]($ex + $er * 0.2), [single]($ey - $er), [single]($er * 2), [single]($er * 2))
        }
        $g.FillEllipse($inkBrush, [single]($cx - $r), [single]($cy - $r), [single]($r * 2), [single]($r * 1.9))
        $g.FillRectangle($inkBrush, [single]($cx - $r * 0.6), [single]($cy + $r * 0.6), [single]($r * 1.2), [single]($r * 0.7))
        $er = $r * 0.32
        $g.FillEllipse($white, [single]($cx - $r * 0.55 - $er), [single]($cy - $er * 0.6), [single]($er * 2), [single]($er * 2))
        $g.FillEllipse($white, [single]($cx + $r * 0.55 - $er), [single]($cy - $er * 0.6), [single]($er * 2), [single]($er * 2))
        $g.FillRectangle($white, [single]($cx - $r * 0.08), [single]($cy + $r * 0.45), [single]($r * 0.16), [single]($r * 0.25))
    }
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
if (-not (Test-Path "$root\sce_sys\icon0.png")) { Save-Art "$root\sce_sys\icon0.png" 512 512 $true; Write-Host "olusturuldu: sce_sys\icon0.png" }
if (-not (Test-Path "$root\sce_sys\pic1.png"))  { Save-Art "$root\sce_sys\pic1.png" 1920 1080 $false; Write-Host "olusturuldu: sce_sys\pic1.png" }

Write-Host "Hazirlik tamam. Simdi WSL icinde: make"
