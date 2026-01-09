$ErrorActionPreference = "Stop"

Write-Host "Generating test certificates..." -ForegroundColor Green

if (-not $PSScriptRoot) {
    $PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
}

$certsDir = Join-Path $PSScriptRoot "certs"
if (-not (Test-Path $certsDir)) {
    New-Item -ItemType Directory -Path $certsDir | Out-Null
    Write-Host "Created directory: $certsDir" -ForegroundColor Yellow
}

$openssl = "openssl"
if (-not (Get-Command $openssl -ErrorAction SilentlyContinue)) {
    if (Test-Path "C:\msys64\ucrt64\bin\openssl.exe") {
        $openssl = "C:\msys64\ucrt64\bin\openssl.exe"
    } else {
        Write-Host "Error: OpenSSL not found in PATH" -ForegroundColor Red
        Write-Host "Please install OpenSSL or add it to your PATH" -ForegroundColor Red
        exit 1
    }
}

Push-Location $certsDir
$errorOccurred = $false

try {
    Write-Host ""
    Write-Host "1. Generating valid certificate (valid for 365 days)..." -ForegroundColor Cyan
    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    $validArgs = @("req", "-x509", "-newkey", "rsa:2048", "-keyout", "valid.key", "-out", "valid.pem", "-days", "365", "-nodes", "-subj", "/CN=test-valid.example.com/O=Test/C=US")
    $process = Start-Process -FilePath $openssl -ArgumentList $validArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    
    if ($process.ExitCode -ne 0) {
        Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
        Write-Host "   ✗ Failed to create valid.pem" -ForegroundColor Red
        throw "Failed to create valid.pem"
    }
    Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    Write-Host "   ✓ Created valid.pem" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "2. Generating expired certificate (expired in the past)..." -ForegroundColor Cyan
    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    
    $expiredArgs = @("req", "-x509", "-newkey", "rsa:2048", "-keyout", "expired.key", "-out", "expired_temp.pem", "-days", "365", "-nodes", "-subj", "/CN=test-expired.example.com/O=Test/C=US")
    $process = Start-Process -FilePath $openssl -ArgumentList $expiredArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    
    if ($process.ExitCode -ne 0) {
        Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
        Write-Host "   ✗ Failed to create temporary expired certificate" -ForegroundColor Red
        throw "Failed to create temporary expired certificate"
    }
    
    $notBefore = "20200101000000Z"
    $notAfter = "20250101000000Z"
    
    $modifyArgs = @("x509", "-in", "expired_temp.pem", "-out", "expired.pem", "-signkey", "expired.key", "-not_before", $notBefore, "-not_after", $notAfter)
    Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    $process2 = Start-Process -FilePath $openssl -ArgumentList $modifyArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    
    if ($process2.ExitCode -ne 0) {
        $errorContent = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
        Remove-Item expired_temp.pem -ErrorAction SilentlyContinue
        Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
        Write-Host "   ✗ Failed to modify expired certificate dates" -ForegroundColor Red
        Write-Host "   Error: $errorContent" -ForegroundColor Red
        throw "Failed to modify expired certificate dates: $errorContent"
    }
    
    Remove-Item expired_temp.pem -ErrorAction SilentlyContinue
    Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    Write-Host "   ✓ Created expired.pem (expired on 2025-01-01)" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "3. Creating malformed certificate..." -ForegroundColor Cyan
    $malformedContent = "-----BEGIN CERTIFICATE-----`r`nThis is not a valid certificate`r`n-----END CERTIFICATE-----"
    $malformedContent | Out-File -FilePath malformed.pem -Encoding ASCII -NoNewline
    
    if (-not (Test-Path malformed.pem)) {
        Write-Host "   ✗ Failed to create malformed.pem" -ForegroundColor Red
        throw "Failed to create malformed.pem"
    }
    Write-Host "   ✓ Created malformed.pem" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "Cleaning up private key files..." -ForegroundColor Cyan
    Remove-Item -Path valid.key -ErrorAction SilentlyContinue
    Remove-Item -Path expired.key -ErrorAction SilentlyContinue
    
    Write-Host ""
    Write-Host "✓ All test certificates generated successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Generated files:" -ForegroundColor Yellow
    Write-Host "  - $certsDir\valid.pem" -ForegroundColor White
    Write-Host "  - $certsDir\expired.pem" -ForegroundColor White
    Write-Host "  - $certsDir\malformed.pem" -ForegroundColor White
    
} catch {
    Write-Host ""
    Write-Host "Error: $_" -ForegroundColor Red
    $errorOccurred = $true
} finally {
    Pop-Location
}

if ($errorOccurred) {
    exit 1
}
