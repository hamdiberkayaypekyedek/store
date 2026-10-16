# PKG Magaza - yeni surum yayinlama (Windows PowerShell)
#
# Derlenmis pkg'yi GitHub release olarak yukler ve repodaki version.json'u gunceller.
# Boylece PS4'teki uygulama acilista yeni surumu gorur. GitHub CLI (gh) gerekir.
#
#   .\yayinla.ps1 -Pkg ..\IV0000-PKGM00001_00-PKGMAGAZA0000000.pkg -Version 01.01 -Notlar "Disk kopyalama eklendi"
param(
    [Parameter(Mandatory = $true)][string]$Pkg,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Baslik = "PKG Magaza",
    [string]$Notlar = ""
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI (gh) bulunamadi. https://cli.github.com adresinden kurun ve 'gh auth login' yapin."
}
if (-not (Test-Path $Pkg)) { throw "pkg bulunamadi: $Pkg" }

$asset = "PKGMagaza.pkg"
$tag = "v$Version"
$repo = (gh repo view --json nameWithOwner --jq .nameWithOwner)
Write-Host "Repo: $repo   Etiket: $tag"

# 1) Release olustur (varsa dosyayi uzerine ekle) ve pkg'yi yukle
$exists = (gh release view $tag 2>$null)
if ($LASTEXITCODE -eq 0) {
    gh release upload $tag "$Pkg#$asset" --clobber
} else {
    gh release create $tag "$Pkg#$asset" --title "$Baslik $Version" --notes $Notlar
}

# 2) Indirme adresini al ve version.json'u guncelle
$url = "https://github.com/$repo/releases/download/$tag/$asset"
$size = (Get-Item $Pkg).Length
$json = [ordered]@{
    version = $Version
    title   = $Baslik
    size    = $size
    pkg     = $url
    notes   = $Notlar
}
$verPath = Join-Path $root "version.json"
$json | ConvertTo-Json | Out-File $verPath -Encoding utf8
Write-Host "version.json guncellendi -> $verPath"

# 3) version.json'u repoya gonder
Push-Location $root
try {
    git add version.json
    git commit -m "Surum $Version"
    git push
} finally {
    Pop-Location
}
Write-Host "`nBitti. PS4'te uygulamayi acinca guncelleme ekrani cikacak."
