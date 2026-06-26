# TrueAudioRand - esegue l'intero processo passo per passo
# Uso: .\run_process.ps1
#      .\run_process.ps1 -Quick        (demo + 1000 campioni, piu veloce)
#      .\run_process.ps1 -DemoOnly    (solo spiegazione pipeline)

param(
    [switch]$Quick,
    [switch]$DemoOnly,
    [int]$Count = 0
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$MsysBash = "C:\msys64\usr\bin\bash.exe"
$MingwBin = "C:\msys64\mingw64\bin"

function Write-Step($num, $title) {
    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  PASSO $num : $title" -ForegroundColor Cyan
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host ""
}

function Invoke-Make {
    & $MsysBash -lc "export PATH=/mingw64/bin:/usr/bin:`$PATH && cd '$($ProjectDir -replace '\\','/')' && mingw32-make"
    if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita" }
}

$env:PATH = "$MingwBin;$env:PATH"
Set-Location $ProjectDir

if (-not (Test-Path $MsysBash)) {
    Write-Host "MSYS2 non trovato. Installa MSYS2 e riprova." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "TrueAudioRand - Processo completo" -ForegroundColor Green
Write-Host "Cartella: $ProjectDir"
Write-Host ""

Write-Step 1 "COMPILAZIONE"
Write-Host "Compilo libreria, main_test.exe e dipendenze PortAudio..."
Invoke-Make
Write-Host "OK - main_test.exe pronto" -ForegroundColor Green

Write-Step 2 "DEMO PIPELINE (come funziona)"
Write-Host "Mostro un ciclo completo: microfono -> bit -> Von Neumann -> SHA-256 -> uint32"
Write-Host ""
& "$ProjectDir\main_test.exe" --demo
if ($LASTEXITCODE -ne 0) { throw "Demo fallita" }

if ($DemoOnly) {
    Write-Host ""
    Write-Host "Demo completata. Per il processo completo: .\run_process.ps1" -ForegroundColor Yellow
    exit 0
}

$sampleCount = if ($Count -gt 0) { $Count } elseif ($Quick) { 1000 } else { 100000 }

Write-Step 3 "GENERAZIONE CAMPIONI TRNG"
Write-Host "Genero $sampleCount numeri casuali dal microfono..."
Write-Host "(Il microfono deve essere attivo e non muto)"
Write-Host ""
& "$ProjectDir\main_test.exe" --count $sampleCount
if ($LASTEXITCODE -ne 0) { throw "Generazione fallita" }

Write-Step 4 "ANALISI STATISTICA (Python)"
Write-Host "Creo istogramma e scatter plot con verify_rand.py..."
python verify_rand.py random_samples.txt
if ($LASTEXITCODE -ne 0) { throw "Analisi Python fallita" }

Write-Step 5 "RISULTATI"
$files = @("random_samples.txt", "histogram.png", "scatter.png")
foreach ($f in $files) {
    $p = Join-Path $ProjectDir $f
    if (Test-Path $p) {
        $size = (Get-Item $p).Length
        Write-Host "  [OK] $f ($size bytes)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Processo completato." -ForegroundColor Green
Write-Host ""
Write-Host "Comandi utili per ripetere:" -ForegroundColor Yellow
Write-Host "  run_process.bat              processo completo (100k campioni)"
Write-Host "  run_process.bat -Quick       versione rapida (1000 campioni)"
Write-Host "  run_process.bat -DemoOnly    solo spiegazione pipeline"
Write-Host "  .\main_test.exe --demo         demo manuale"
Write-Host "  .\main_test.exe --count 5000   genera 5000 campioni"
Write-Host "  python verify_rand.py          analizza random_samples.txt"
Write-Host ""

if (Test-Path "histogram.png") {
    Start-Process "histogram.png"
    Start-Process "scatter.png"
}
