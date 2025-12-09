# Build script for MSVC (Visual Studio)
# This script sets up the Visual Studio environment and builds with MSVC

param(
    [string]$BuildType = "Release",
    [string]$Generator = "Visual Studio 17 2022"
)

Write-Host "=== Building agent-core with MSVC ===" -ForegroundColor Green
Write-Host ""

# Check for Visual Studio - try multiple methods
$vsPath = $null

# Method 1: Use vswhere if available
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswherePath) {
    Write-Host "Searching for Visual Studio installations..." -ForegroundColor Cyan
    # Try with component requirement first
    $vsPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    
    # If not found, try without component requirement (just find any VS installation)
    if (-not $vsPath) {
        $vsPath = & $vswherePath -latest -products * -property installationPath 2>$null
    }
}

# Method 2: Check common installation paths directly
if (-not $vsPath) {
    $commonPaths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
    )
    
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            $devCmd = Join-Path $path "Common7\Tools\VsDevCmd.bat"
            if (Test-Path $devCmd) {
                $vsPath = $path
                Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green
                break
            }
        }
    }
}

# Method 3: Check if cl.exe is already in PATH (VS environment already set up)
if (-not $vsPath) {
    $clPath = & where.exe cl.exe 2>$null
    if ($clPath) {
        Write-Host "MSVC compiler already available in PATH: $clPath" -ForegroundColor Green
        Write-Host "Skipping Visual Studio environment setup..." -ForegroundColor Cyan
        $vsPath = "PATH"  # Special marker to skip VS setup
    }
}

if (-not $vsPath) {
    Write-Host "ERROR: Visual Studio not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install one of the following:" -ForegroundColor Yellow
    Write-Host "  1. Visual Studio 2022 Community/Professional/Enterprise" -ForegroundColor Cyan
    Write-Host "     Download: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Cyan
    Write-Host "     Select 'Desktop development with C++' workload" -ForegroundColor Cyan
    Write-Host "     IMPORTANT: Wait for installation to complete fully!" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  2. Visual Studio Build Tools 2022" -ForegroundColor Cyan
    Write-Host "     Download: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022" -ForegroundColor Cyan
    Write-Host "     Select 'C++ build tools' workload" -ForegroundColor Cyan
    Write-Host "     IMPORTANT: Wait for installation to complete fully!" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "If Visual Studio is still installing:" -ForegroundColor Yellow
    Write-Host "  - Wait for the installation to finish completely" -ForegroundColor Cyan
    Write-Host "  - Restart your computer after installation" -ForegroundColor Cyan
    Write-Host "  - Then run this script again" -ForegroundColor Cyan
    Write-Host ""
    exit 1
}

if ($vsPath -ne "PATH") {
    Write-Host "Visual Studio found at: $vsPath" -ForegroundColor Green
    
    # Verify Visual Studio installation is complete
    $devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    $vcTools = Join-Path $vsPath "VC\Tools\MSVC"
    
    if (-not (Test-Path $devCmd)) {
        Write-Host "ERROR: VsDevCmd.bat not found at: $devCmd" -ForegroundColor Red
        Write-Host "This usually means Visual Studio installation is incomplete." -ForegroundColor Yellow
        Write-Host "Please complete the Visual Studio installation and try again." -ForegroundColor Yellow
        exit 1
    }
    
    if (-not (Test-Path $vcTools)) {
        Write-Host "WARNING: VC Tools directory not found: $vcTools" -ForegroundColor Yellow
        Write-Host "Visual Studio may not be fully installed. Please check:" -ForegroundColor Yellow
        Write-Host "  1. Open Visual Studio Installer" -ForegroundColor Cyan
        Write-Host "  2. Verify 'Desktop development with C++' workload is installed" -ForegroundColor Cyan
        Write-Host "  3. Click 'Modify' if needed to complete installation" -ForegroundColor Cyan
        Write-Host ""
    }
    
    Write-Host "Setting up Visual Studio environment..." -ForegroundColor Cyan
    
    # Method 1: Try to find and add VS tools to PATH directly (most reliable)
    $vcTools = Join-Path $vsPath "VC\Tools\MSVC"
    if (Test-Path $vcTools) {
        $latestVersion = Get-ChildItem $vcTools -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if ($latestVersion) {
            $clPath = Join-Path $latestVersion.FullName "bin\Hostx64\x64\cl.exe"
            if (Test-Path $clPath) {
                $vcBin = Split-Path -Parent $clPath
                $env:Path = "$vcBin;$env:Path"
                Write-Host "  Added VC tools to PATH: $vcBin" -ForegroundColor Gray
                
                # Also set common VS environment variables
                $vcToolsPath = $latestVersion.FullName
                $env:VCINSTALLDIR = Join-Path $vsPath "VC"
                $env:VCToolsInstallDir = $vcToolsPath
                $env:WindowsSdkDir = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
                
                # Find Windows SDK version
                $sdkDir = $env:WindowsSdkDir
                if (Test-Path $sdkDir) {
                    $sdkVersions = Get-ChildItem (Join-Path $sdkDir "Include") -Directory -ErrorAction SilentlyContinue | 
                        Where-Object { $_.Name -match "^\d+\.\d+" } | 
                        Sort-Object { [Version]$_.Name } -Descending | 
                        Select-Object -First 1
                    if ($sdkVersions) {
                        $env:WindowsSDKVersion = "$($sdkVersions.Name)\"
                        
                        # CRITICAL: Set INCLUDE to only Visual Studio paths (exclude MSYS2)
                        $vcInclude = Join-Path $vcToolsPath "include"
                        $sdkInclude = Join-Path $sdkDir "Include\$($sdkVersions.Name)"
                        $ucrtInclude = Join-Path $sdkDir "Include\$($sdkVersions.Name)\ucrt"
                        $sharedInclude = Join-Path $sdkDir "Include\$($sdkVersions.Name)\shared"
                        $umInclude = Join-Path $sdkDir "Include\$($sdkVersions.Name)\um"
                        $winrtInclude = Join-Path $sdkDir "Include\$($sdkVersions.Name)\winrt"
                        
                        $vsIncludePaths = @()
                        if (Test-Path $vcInclude) { $vsIncludePaths += $vcInclude }
                        if (Test-Path $sdkInclude) { $vsIncludePaths += $sdkInclude }
                        if (Test-Path $ucrtInclude) { $vsIncludePaths += $ucrtInclude }
                        if (Test-Path $sharedInclude) { $vsIncludePaths += $sharedInclude }
                        if (Test-Path $umInclude) { $vsIncludePaths += $umInclude }
                        if (Test-Path $winrtInclude) { $vsIncludePaths += $winrtInclude }
                        
                        if ($vsIncludePaths.Count -gt 0) {
                            $env:INCLUDE = $vsIncludePaths -join ';'
                            Write-Host "  Set INCLUDE to Visual Studio paths only" -ForegroundColor Gray
                        }
                    }
                }
            }
        }
    }
    
    # Method 2: Try to source VsDevCmd.bat (fallback)
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Write-Host "  Attempting to source VsDevCmd.bat..." -ForegroundColor Gray
        $tempFile = Join-Path $env:TEMP "vs-env-$(Get-Random).txt"
        try {
            # Use cmd to source VsDevCmd and export environment
            $cmdScript = @"
@echo off
call "$devCmd" -arch=x64 -host_arch=x64 >nul 2>&1
where cl.exe > "$tempFile" 2>&1
"@
            $tempBat = Join-Path $env:TEMP "vs-cmd-$(Get-Random).bat"
            $cmdScript | Out-File -FilePath $tempBat -Encoding ASCII
            & cmd.exe /c $tempBat
            Remove-Item $tempBat -Force -ErrorAction SilentlyContinue
            
            if (Test-Path $tempFile) {
                $clPathFromCmd = Get-Content $tempFile -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($clPathFromCmd -and (Test-Path $clPathFromCmd)) {
                    $vcBin = Split-Path -Parent $clPathFromCmd
                    $env:Path = "$vcBin;$env:Path"
                    Write-Host "  Found cl.exe via VsDevCmd: $clPathFromCmd" -ForegroundColor Gray
                }
                Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
            }
        } catch {
            # Ignore errors
        }
    }
}

# Verify MSVC compiler is available
$clPath = $null

# Try multiple methods to find cl.exe
try {
    $clPath = & where.exe cl.exe 2>$null | Select-Object -First 1
} catch {
    # Ignore
}

if (-not $clPath) {
    try {
        $clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
        if ($clCmd) {
            $clPath = $clCmd.Source
        }
    } catch {
        # Ignore
    }
}

# If still not found, try to find it directly from VS path
if (-not $clPath -and $vsPath -ne "PATH") {
    $vcTools = Join-Path $vsPath "VC\Tools\MSVC"
    if (Test-Path $vcTools) {
        $latestVersion = Get-ChildItem $vcTools -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if ($latestVersion) {
            $potentialClPath = Join-Path $latestVersion.FullName "bin\Hostx64\x64\cl.exe"
            if (Test-Path $potentialClPath) {
                $clPath = $potentialClPath
                $vcBin = Split-Path -Parent $clPath
                if ($env:Path -notlike "*$vcBin*") {
                    $env:Path = "$vcBin;$env:Path"
                }
                Write-Host "  Found cl.exe directly: $clPath" -ForegroundColor Gray
            }
        }
    }
}

# Final check - verify the path exists
if ($clPath -and (Test-Path $clPath)) {
    Write-Host "MSVC compiler found: $clPath" -ForegroundColor Green
} else {
    Write-Host "ERROR: cl.exe (MSVC compiler) not found" -ForegroundColor Red
    Write-Host "" -ForegroundColor Red
    Write-Host "Troubleshooting:" -ForegroundColor Yellow
    Write-Host "  1. Verify Visual Studio is installed with C++ tools" -ForegroundColor Cyan
    Write-Host "  2. Try opening 'Developer Command Prompt for VS 2022' from Start Menu" -ForegroundColor Cyan
    Write-Host "  3. Then run: .\scripts\build-msvc.ps1" -ForegroundColor Cyan
    Write-Host "" -ForegroundColor Yellow
    Write-Host "Or build manually:" -ForegroundColor Yellow
    Write-Host "  cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release" -ForegroundColor Cyan
    Write-Host "  cmake --build build --config Release" -ForegroundColor Cyan
    exit 1
}

Write-Host "MSVC compiler found: $clPath" -ForegroundColor Green
Write-Host ""

# Get script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$buildDir = Join-Path $projectRoot "build"

# Stop any running agent-core processes to prevent file locking
Write-Host "Checking for running agent-core processes..." -ForegroundColor Cyan
$runningProcesses = Get-Process -Name "agent-core" -ErrorAction SilentlyContinue
if ($runningProcesses) {
    Write-Host "  Found $($runningProcesses.Count) running agent-core process(es), stopping..." -ForegroundColor Yellow
    $runningProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    Write-Host "  Stopped all agent-core processes" -ForegroundColor Gray
} else {
    Write-Host "  No running agent-core processes found" -ForegroundColor Gray
}

# Stop the service if it's running
$service = Get-Service -Name "AgentCore" -ErrorAction SilentlyContinue
if ($service -and $service.Status -eq "Running") {
    Write-Host "  AgentCore service is running, stopping..." -ForegroundColor Yellow
    try {
        Stop-Service -Name "AgentCore" -Force -ErrorAction Stop
        Start-Sleep -Seconds 2
        Write-Host "  Service stopped" -ForegroundColor Gray
    } catch {
        Write-Host "  Warning: Could not stop service: $_" -ForegroundColor Yellow
    }
}

# Clean build directory if it exists
if (Test-Path $buildDir) {
    Write-Host "Cleaning build directory..." -ForegroundColor Cyan
    
    # Try to remove the executable specifically if it exists and is locked
    $exePath = Join-Path $buildDir "Release\agent-core.exe"
    if (Test-Path $exePath) {
        # Try to unlock the file by removing read-only attribute
        try {
            $file = Get-Item $exePath -ErrorAction SilentlyContinue
            if ($file) {
                $file.Attributes = $file.Attributes -band (-bnot [System.IO.FileAttributes]::ReadOnly)
            }
        } catch {
            # Ignore
        }
    }
    
    # Also check Debug build
    $debugExePath = Join-Path $buildDir "Debug\agent-core.exe"
    if (Test-Path $debugExePath) {
        try {
            $file = Get-Item $debugExePath -ErrorAction SilentlyContinue
            if ($file) {
                $file.Attributes = $file.Attributes -band (-bnot [System.IO.FileAttributes]::ReadOnly)
            }
        } catch {
            # Ignore
        }
    }
    
    # Wait a moment for file handles to close
    Start-Sleep -Milliseconds 500
    
    Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
}

# Windows builds use MSVC only (not MSYS2/MinGW)
# Defensive: Remove MSYS2/MinGW paths from environment variables if present
Write-Host "Cleaning MSYS2/MinGW paths from environment (defensive, should not be needed)..." -ForegroundColor Cyan

# Clean PATH and CMAKE_PREFIX_PATH (INCLUDE and LIB are set above by VS environment setup)
if ($env:PATH) {
    $pathEntries = $env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch '[Mm][Ss][Yy][Ss]' -and $_ -notmatch '[Mm][Ii][Nn][Gg][Ww]' }
    $env:PATH = $pathEntries -join ';'
    Write-Host "  Cleaned PATH: removed MSYS2/MinGW paths" -ForegroundColor Gray
}

# CRITICAL: Clear CMAKE_PREFIX_PATH completely if it contains MSYS2
# CMake uses this to find packages, and MSYS2 paths cause it to find wrong packages
if ($env:CMAKE_PREFIX_PATH) {
    $prefixPaths = $env:CMAKE_PREFIX_PATH -split ';' | Where-Object { $_ -and $_ -notmatch '[Mm][Ss][Yy][Ss]' -and $_ -notmatch '[Mm][Ii][Nn][Gg][Ww]' }
    if ($prefixPaths.Count -gt 0) {
        $env:CMAKE_PREFIX_PATH = $prefixPaths -join ';'
        Write-Host "  Cleaned CMAKE_PREFIX_PATH: removed MSYS2/MinGW paths" -ForegroundColor Gray
    } else {
        # If all paths were MSYS2, clear it completely
        Remove-Item Env:\CMAKE_PREFIX_PATH -ErrorAction SilentlyContinue
        Write-Host "  Cleared CMAKE_PREFIX_PATH (contained only MSYS2/MinGW paths)" -ForegroundColor Gray
    }
}

# Also clear CMAKE_FIND_ROOT_PATH if it exists and contains MSYS2
if ($env:CMAKE_FIND_ROOT_PATH) {
    $rootPaths = $env:CMAKE_FIND_ROOT_PATH -split ';' | Where-Object { $_ -and $_ -notmatch '[Mm][Ss][Yy][Ss]' -and $_ -notmatch '[Mm][Ii][Nn][Gg][Ww]' }
    if ($rootPaths.Count -gt 0) {
        $env:CMAKE_FIND_ROOT_PATH = $rootPaths -join ';'
    } else {
        Remove-Item Env:\CMAKE_FIND_ROOT_PATH -ErrorAction SilentlyContinue
    }
}

# Verify INCLUDE doesn't contain MSYS2 paths (it should only have VS paths from above)
if ($env:INCLUDE) {
    $includeCheck = $env:INCLUDE -split ';' | Where-Object { $_ -match '[Mm][Ss][Yy][Ss]' -or $_ -match '[Mm][Ii][Nn][Gg][Ww]' }
    if ($includeCheck) {
        Write-Host "  WARNING: INCLUDE still contains MSYS2/MinGW paths, filtering..." -ForegroundColor Yellow
        $includePaths = $env:INCLUDE -split ';' | Where-Object { $_ -and $_ -notmatch '[Mm][Ss][Yy][Ss]' -and $_ -notmatch '[Mm][Ii][Nn][Gg][Ww]' }
        $env:INCLUDE = $includePaths -join ';'
    } else {
        Write-Host "  INCLUDE verified: no MSYS2/MinGW paths" -ForegroundColor Gray
    }
}

# Check for vcpkg and configure toolchain file
$vcpkgToolchain = $null
$vcpkgJson = Join-Path $projectRoot "vcpkg.json"
if (Test-Path $vcpkgJson) {
    Write-Host "vcpkg.json found - using vcpkg manifest mode" -ForegroundColor Cyan
    
    # Try to find vcpkg installation
    # Method 1: Check VCPKG_ROOT environment variable
    if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
        $vcpkgToolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $vcpkgToolchain) {
            Write-Host "  Using vcpkg from VCPKG_ROOT: $env:VCPKG_ROOT" -ForegroundColor Gray
        } else {
            $vcpkgToolchain = $null
        }
    }
    
    # Method 2: Check Visual Studio installation directory (common location)
    if (-not $vcpkgToolchain -and $vsPath -ne "PATH" -and $vsPath) {
        $vsVcpkgPath = Join-Path $vsPath "VC\vcpkg"
        $toolchain = Join-Path $vsVcpkgPath "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $toolchain) {
            $vcpkgToolchain = $toolchain
            Write-Host "  Found vcpkg in Visual Studio installation: $vsVcpkgPath" -ForegroundColor Gray
        }
    }
    
    # Method 3: Check common vcpkg installation locations
    if (-not $vcpkgToolchain) {
        $commonVcpkgPaths = @(
            "$env:USERPROFILE\vcpkg",
            "$env:ProgramFiles\vcpkg",
            "$env:ProgramFiles(x86)\vcpkg",
            "C:\vcpkg",
            "C:\tools\vcpkg"
        )
        foreach ($vcpkgPath in $commonVcpkgPaths) {
            $toolchain = Join-Path $vcpkgPath "scripts\buildsystems\vcpkg.cmake"
            if (Test-Path $toolchain) {
                $vcpkgToolchain = $toolchain
                Write-Host "  Found vcpkg at: $vcpkgPath" -ForegroundColor Gray
                break
            }
        }
    }
    
    # Method 4: If vcpkg is in PATH, try to find it
    if (-not $vcpkgToolchain) {
        $vcpkgExe = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
        if ($vcpkgExe) {
            $vcpkgDir = Split-Path (Split-Path $vcpkgExe.Path)
            $toolchain = Join-Path $vcpkgDir "scripts\buildsystems\vcpkg.cmake"
            if (Test-Path $toolchain) {
                $vcpkgToolchain = $toolchain
                Write-Host "  Found vcpkg in PATH: $vcpkgDir" -ForegroundColor Gray
            }
        }
    }
    
    if (-not $vcpkgToolchain) {
        Write-Host "WARNING: vcpkg.json found but vcpkg toolchain file not found" -ForegroundColor Yellow
        Write-Host "  vcpkg will be installed automatically in manifest mode if CMake integration is enabled" -ForegroundColor Gray
        Write-Host "  If build fails, install vcpkg: https://github.com/microsoft/vcpkg#quick-start-windows" -ForegroundColor Gray
    }
}

# Configure CMake
Write-Host "Configuring CMake with $Generator..." -ForegroundColor Cyan
Push-Location $projectRoot
try {
    # Try to configure - CMake will use its own VS detection if generator is available
    # Use proper quoting for generator name
    $cmakeArgs = @("-S", ".", "-B", "build", "-G", $Generator, "-A", "x64", "-DCMAKE_BUILD_TYPE=$BuildType")
    
    # Add vcpkg toolchain file if found
    if ($vcpkgToolchain) {
        $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
        Write-Host "  Using vcpkg toolchain: $vcpkgToolchain" -ForegroundColor Gray
    }
    
    # If VS path was found, try to help CMake find it
    if ($vsPath -ne "PATH" -and $vsPath) {
        $cmakeArgs += "-DCMAKE_GENERATOR_INSTANCE=$vsPath"
    }
    
    $cmakeOutput = & cmake @cmakeArgs 2>&1
    $cmakeExitCode = $LASTEXITCODE
    
    if ($cmakeExitCode -ne 0) {
        Write-Host "CMake output:" -ForegroundColor Yellow
        Write-Host $cmakeOutput -ForegroundColor Yellow
        Write-Host ""
        
        # Try alternative: let CMake auto-detect (without specifying generator)
        Write-Host "Trying CMake auto-detection..." -ForegroundColor Cyan
        $cmakeArgs = @("-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=$BuildType")
        if ($vcpkgToolchain) {
            $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
        }
        if ($vsPath -ne "PATH" -and $vsPath) {
            $cmakeArgs += "-DCMAKE_GENERATOR_INSTANCE=$vsPath"
        }
        $cmakeOutput = & cmake @cmakeArgs 2>&1
        $cmakeExitCode = $LASTEXITCODE
        
        if ($cmakeExitCode -ne 0) {
            Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
            Write-Host ""
            Write-Host "Troubleshooting:" -ForegroundColor Yellow
            Write-Host "  1. Open 'Developer Command Prompt for VS 2022' from Start Menu" -ForegroundColor Cyan
            Write-Host "  2. Navigate to: $projectRoot" -ForegroundColor Cyan
            Write-Host "  3. Run: cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Release" -ForegroundColor Cyan
            Write-Host "  4. Run: cmake --build build --config Release" -ForegroundColor Cyan
            exit 1
        } else {
            Write-Host "CMake auto-detected Visual Studio generator" -ForegroundColor Green
        }
    }
    
    # Build
    Write-Host ""
    Write-Host "Building project..." -ForegroundColor Cyan
    cmake --build build --config $BuildType
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Build failed" -ForegroundColor Red
        exit 1
    }
    
    Write-Host ""
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host "Executable: $(Join-Path $buildDir "$BuildType\agent-core.exe")" -ForegroundColor Cyan
    
} finally {
    Pop-Location
}

