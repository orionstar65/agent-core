# PowerShell script to manage Agent Core Windows Service
# Supports: install, uninstall, start, stop, status, viewlog
# Requires Administrator privileges for install/uninstall
#
# If you get an execution policy error, run with:
#   powershell -ExecutionPolicy Bypass -File .\agent-core.service.ps1 <action>
# Or change execution policy:
#   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("install", "uninstall", "start", "stop", "restart", "status", "viewlog")]
    [string]$Action,
    
    [Parameter(Mandatory=$false)]
    [string]$ServiceName = "AgentCore",
    
    [Parameter(Mandatory=$false)]
    [string]$DisplayName = "Agent Core IoT Service",
    
    [Parameter(Mandatory=$false)]
    [string]$Description = "Cross-platform IoT Agent Core service for device management",
    
    [Parameter(Mandatory=$false)]
    [string]$BinaryPath,
    
    [Parameter(Mandatory=$false)]
    [string]$ConfigPath = "config\dev.json",
    
    [Parameter(Mandatory=$false)]
    [ValidateSet("Automatic", "Manual", "Disabled")]
    [string]$StartupType = "Automatic",
    
    [Parameter(Mandatory=$false)]
    [string]$NssmPath = "nssm",
    
    [Parameter(Mandatory=$false)]
    [int]$Lines = 50,
    
    [Parameter(Mandatory=$false)]
    [switch]$Follow,
    
    [Parameter(Mandatory=$false)]
    [switch]$Events,
    
    [Parameter(Mandatory=$false)]
    [switch]$All
)

# ============================================================================
# INSTALL FUNCTIONS
# ============================================================================

function Install-AgentCoreService {
    # Check for administrator privileges
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "Error: Installation requires Administrator privileges." -ForegroundColor Red
        Write-Host "Please run PowerShell as Administrator and try again." -ForegroundColor Yellow
        return 1
    }
    
    # Check for NSSM
    $nssmExe = $null
    $useNssm = $false
    
    if ($NssmPath -eq "nssm") {
        $nssmExe = Get-Command nssm.exe -ErrorAction SilentlyContinue
        if ($nssmExe) {
            $nssmExe = $nssmExe.Source
            $useNssm = $true
        }
    } else {
        if (Test-Path $NssmPath) {
            $nssmExe = $NssmPath
            $useNssm = $true
        } elseif (Test-Path "$NssmPath\nssm.exe") {
            $nssmExe = "$NssmPath\nssm.exe"
            $useNssm = $true
        }
    }
    
    if ($useNssm) {
        Write-Host "NSSM found: $nssmExe" -ForegroundColor Green
        Write-Host "Using NSSM for service installation (recommended)" -ForegroundColor Cyan
    } else {
        Write-Host "NSSM not found - falling back to native SCM" -ForegroundColor Yellow
        Write-Host "Note: NSSM provides better argument handling. Download from: https://nssm.cc/download" -ForegroundColor Gray
    }
    Write-Host ""
    
    # Determine binary path and build directory
    if (-not $BinaryPath) {
        $scriptDir = $PSScriptRoot
        if (-not $scriptDir) {
            $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        }
        $projectRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
        
        # Try Release build first, then Debug, then root build directory
        $possiblePaths = @(
            (Join-Path $projectRoot "build\Release\agent-core.exe"),
            (Join-Path $projectRoot "build\Debug\agent-core.exe"),
            (Join-Path $projectRoot "build\agent-core.exe")
        )
        
        $buildPath = $null
        foreach ($path in $possiblePaths) {
            if (Test-Path $path) {
                $buildPath = $path
                break
            }
        }
        
        if (-not $buildPath) {
            Write-Host "Error: Binary not found in any of these locations:" -ForegroundColor Red
            foreach ($path in $possiblePaths) {
                Write-Host "  - $path" -ForegroundColor Yellow
            }
            Write-Host "Please build the project first or specify -BinaryPath" -ForegroundColor Yellow
            return 1
        }
        
        $buildDir = Split-Path -Parent $buildPath
        
        # Convert buildPath to absolute path
        if (-not [System.IO.Path]::IsPathRooted($buildPath)) {
            $buildPath = (Resolve-Path $buildPath -ErrorAction Stop).Path
        } else {
            $buildPath = (Resolve-Path $buildPath -ErrorAction Stop).Path
        }
        
        $BinaryPath = $buildPath
    } else {
        # Extract build directory from BinaryPath
        if (-not [System.IO.Path]::IsPathRooted($BinaryPath)) {
            $BinaryPath = (Resolve-Path $BinaryPath -ErrorAction Stop).Path
        } else {
            $BinaryPath = (Resolve-Path $BinaryPath -ErrorAction Stop).Path
        }
        $buildDir = Split-Path -Parent $BinaryPath
    }
    
    # Ensure buildDir is set
    if (-not $buildDir) {
        $scriptDir = $PSScriptRoot
        if (-not $scriptDir) {
            $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        }
        $projectRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
        $buildDir = Join-Path $projectRoot "build"
    }
    
    # Resolve config path relative to build directory
    if (-not [System.IO.Path]::IsPathRooted($ConfigPath)) {
        $resolvedConfigPath = Join-Path $buildDir $ConfigPath
        if (-not (Test-Path $resolvedConfigPath)) {
            # Try relative to project root
            $scriptDir = $PSScriptRoot
            if (-not $scriptDir) {
                $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
            }
            $projectRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
            $resolvedConfigPath = Join-Path $projectRoot $ConfigPath
        }
        $ConfigPath = $resolvedConfigPath
    }
    
    # Ensure config file exists
    if (-not (Test-Path $ConfigPath)) {
        Write-Host "Warning: Config file not found at $ConfigPath" -ForegroundColor Yellow
        Write-Host "Service may fail to start if config is required." -ForegroundColor Yellow
    }
    
    # Display configuration
    Write-Host "=== Service Installation Configuration ===" -ForegroundColor Green
    Write-Host "  Service Name: $ServiceName" -ForegroundColor Cyan
    Write-Host "  Display Name: $DisplayName" -ForegroundColor Cyan
    Write-Host "  Binary Path: $BinaryPath" -ForegroundColor Cyan
    Write-Host "  Config Path: $ConfigPath" -ForegroundColor Cyan
    Write-Host "  Working Directory: $buildDir" -ForegroundColor Cyan
    Write-Host "  Startup Type: $StartupType" -ForegroundColor Cyan
    Write-Host "  Method: $(if ($useNssm) { 'NSSM' } else { 'SCM (with environment variable)' })" -ForegroundColor Cyan
    Write-Host ""
    
    # Check if service already exists
    $existingService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($existingService) {
        Write-Host "Service '$ServiceName' already exists." -ForegroundColor Yellow
        $response = Read-Host "Do you want to uninstall the existing service first? (Y/N)"
        if ($response -eq "Y" -or $response -eq "y") {
            Write-Host "Stopping existing service..." -ForegroundColor Yellow
            if ($useNssm) {
                & $nssmExe stop $ServiceName | Out-Null
            } else {
                Stop-Service -Name $ServiceName -ErrorAction SilentlyContinue
            }
            Start-Sleep -Seconds 2
            
            Write-Host "Removing existing service..." -ForegroundColor Yellow
            if ($useNssm) {
                & $nssmExe remove $ServiceName confirm | Out-Null
            } else {
                sc.exe delete $ServiceName | Out-Null
            }
            Start-Sleep -Seconds 2
        } else {
            Write-Host "Installation cancelled." -ForegroundColor Yellow
            return 0
        }
    }
    
    # Install service
    if ($useNssm) {
        # Install using NSSM
        Write-Host "Installing service with NSSM..." -ForegroundColor Green
        try {
            # NSSM install command: nssm install <ServiceName> <PathToExe> [Arguments]
            $installArgs = @("install", $ServiceName, $BinaryPath, "--config", $ConfigPath)
            $result = & $nssmExe @installArgs
            if ($LASTEXITCODE -ne 0) {
                Write-Host "Error installing service. Exit code: $LASTEXITCODE" -ForegroundColor Red
                Write-Host "Output: $result" -ForegroundColor Red
                return 1
            }
            
            Write-Host "Service installed successfully!" -ForegroundColor Green
            Write-Host ""
            
            # Configure service properties
            Write-Host "Configuring service properties..." -ForegroundColor Green
            
            # Set display name
            & $nssmExe set $ServiceName DisplayName $DisplayName | Out-Null
            
            # Set description
            & $nssmExe set $ServiceName Description $Description | Out-Null
            
            # Set working directory
            & $nssmExe set $ServiceName AppDirectory $buildDir | Out-Null
            
            # Set startup type
            $startType = switch ($StartupType) {
                "Automatic" { "SERVICE_AUTO_START" }
                "Manual" { "SERVICE_DEMAND_START" }
                "Disabled" { "SERVICE_DISABLED" }
            }
            & $nssmExe set $ServiceName Start $startType | Out-Null
            
            Write-Host "Service configuration complete!" -ForegroundColor Green
            Write-Host ""
            
        } catch {
            Write-Host "Error installing service: $_" -ForegroundColor Red
            return 1
        }
    } else {
        # Install using SCM with environment variable
        Write-Host "Installing service with SCM (using environment variable)..." -ForegroundColor Green
        Write-Host "  Using AGENT_CORE_CONFIG_PATH environment variable to pass config path" -ForegroundColor Gray
        Write-Host ""
        
        try {
            # Install service with executable directly
            New-Service -Name $ServiceName `
                        -BinaryPathName $BinaryPath `
                        -DisplayName $DisplayName `
                        -Description $Description `
                        -StartupType $StartupType | Out-Null
            
            # Set environment variable in service registry
            $regPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$ServiceName"
            if (Test-Path $regPath) {
                # Set environment variables
                $logPath = Join-Path $buildDir "agent-core-service.log"
                $envValue = @("AGENT_CORE_CONFIG_PATH=$ConfigPath", "AGENT_CORE_LOG_PATH=$logPath")
                Set-ItemProperty -Path $regPath -Name "Environment" -Value $envValue -Type MultiString -ErrorAction Stop
                
                Write-Host "Environment variables set:" -ForegroundColor Gray
                Write-Host "  AGENT_CORE_CONFIG_PATH=$ConfigPath" -ForegroundColor Gray
                Write-Host "  AGENT_CORE_LOG_PATH=$logPath" -ForegroundColor Gray
                Write-Host ""
                
                # Set working directory in registry
                Set-ItemProperty -Path $regPath -Name "WorkingDirectory" -Value $buildDir -ErrorAction SilentlyContinue
                Write-Host "Working directory set to: $buildDir" -ForegroundColor Gray
            }
            
            Write-Host "Service installed successfully!" -ForegroundColor Green
            Write-Host ""
            Write-Host "NOTE: Config path is passed via environment variable (no batch file needed)" -ForegroundColor Gray
            Write-Host ""
            
        } catch {
            Write-Host "Error installing service: $_" -ForegroundColor Red
            Write-Host ""
            Write-Host "Troubleshooting:" -ForegroundColor Yellow
            Write-Host "  1. Check that executable path is correct: $BinaryPath" -ForegroundColor Cyan
            Write-Host "  2. Verify config file exists: $ConfigPath" -ForegroundColor Cyan
            Write-Host "  3. Ensure you have Administrator privileges" -ForegroundColor Cyan
            return 1
        }
    }
    
    # Show service status
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Host "Service Status: $($service.Status)" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "To start the service, run:" -ForegroundColor Yellow
        Write-Host "  .\packaging\windows\agent-core.service.ps1 start" -ForegroundColor White
    }
    
    return 0
}

# ============================================================================
# UNINSTALL FUNCTIONS
# ============================================================================

function Uninstall-AgentCoreService {
    # Check for administrator privileges
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "Error: Uninstallation requires Administrator privileges." -ForegroundColor Red
        Write-Host "Please run PowerShell as Administrator and try again." -ForegroundColor Yellow
        return 1
    }
    
    Write-Host "Uninstalling Agent Core Windows Service..." -ForegroundColor Green
    Write-Host "  Service Name: $ServiceName" -ForegroundColor Cyan
    Write-Host ""
    
    # Check if service exists
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $service) {
        Write-Host "Service '$ServiceName' not found." -ForegroundColor Yellow
        return 0
    }
    
    # Stop the service if running
    if ($service.Status -eq "Running") {
        Write-Host "Stopping service..." -ForegroundColor Yellow
        try {
            Stop-Service -Name $ServiceName -Force
            $timeout = 30
            $elapsed = 0
            while ($service.Status -ne "Stopped" -and $elapsed -lt $timeout) {
                Start-Sleep -Seconds 1
                $service.Refresh()
                $elapsed++
            }
            
            if ($service.Status -eq "Stopped") {
                Write-Host "Service stopped successfully." -ForegroundColor Green
            } else {
                Write-Host "Warning: Service did not stop within timeout period." -ForegroundColor Yellow
                Write-Host "Attempting to remove service anyway..." -ForegroundColor Yellow
            }
        } catch {
            Write-Host "Warning: Error stopping service: $_" -ForegroundColor Yellow
            Write-Host "Attempting to remove service anyway..." -ForegroundColor Yellow
        }
    }
    
    # Remove the service
    Write-Host "Removing service..." -ForegroundColor Yellow
    try {
        $result = sc.exe delete $ServiceName
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Service uninstalled successfully!" -ForegroundColor Green
        } else {
            Write-Host "Error removing service. Exit code: $LASTEXITCODE" -ForegroundColor Red
            Write-Host "Output: $result" -ForegroundColor Red
            return 1
        }
    } catch {
        Write-Host "Error removing service: $_" -ForegroundColor Red
        return 1
    }
    
    # Verify removal
    Start-Sleep -Seconds 1
    $verifyService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($verifyService) {
        Write-Host "Warning: Service still exists after removal attempt." -ForegroundColor Yellow
        return 1
    } else {
        Write-Host "Service removal verified." -ForegroundColor Green
    }
    
    return 0
}

# ============================================================================
# SERVICE CONTROL FUNCTIONS
# ============================================================================

function Start-AgentCoreService {
    Write-Host "Starting Agent Core service..." -ForegroundColor Green
    
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $service) {
        Write-Host "Error: Service '$ServiceName' not found." -ForegroundColor Red
        Write-Host "Please install the service first using: .\agent-core.service.ps1 install" -ForegroundColor Yellow
        return 1
    }
    
    if ($service.Status -eq "Running") {
        Write-Host "Service is already running." -ForegroundColor Yellow
        return 0
    }
    
    try {
        Start-Service -Name $ServiceName
        Start-Sleep -Seconds 1
        $service.Refresh()
        
        if ($service.Status -eq "Running") {
            Write-Host "Service started successfully!" -ForegroundColor Green
            return 0
        } else {
            Write-Host "Warning: Service status is $($service.Status)" -ForegroundColor Yellow
            return 1
        }
    } catch {
        Write-Host "Error starting service: $_" -ForegroundColor Red
        return 1
    }
}

function Stop-AgentCoreService {
    Write-Host "Stopping Agent Core service..." -ForegroundColor Green
    
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $service) {
        Write-Host "Error: Service '$ServiceName' not found." -ForegroundColor Red
        return 1
    }
    
    if ($service.Status -eq "Stopped") {
        Write-Host "Service is already stopped." -ForegroundColor Yellow
        return 0
    }
    
    try {
        Stop-Service -Name $ServiceName -Force
        $timeout = 30
        $elapsed = 0
        while ($service.Status -ne "Stopped" -and $elapsed -lt $timeout) {
            Start-Sleep -Seconds 1
            $service.Refresh()
            $elapsed++
        }
        
        if ($service.Status -eq "Stopped") {
            Write-Host "Service stopped successfully!" -ForegroundColor Green
            return 0
        } else {
            Write-Host "Warning: Service did not stop within timeout period." -ForegroundColor Yellow
            return 1
        }
    } catch {
        Write-Host "Error stopping service: $_" -ForegroundColor Red
        return 1
    }
}

function Restart-AgentCoreService {
    Write-Host "Restarting Agent Core service..." -ForegroundColor Green
    Stop-AgentCoreService | Out-Null
    Start-Sleep -Seconds 2
    return Start-AgentCoreService
}

function Get-AgentCoreServiceStatus {
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $service) {
        Write-Host "Service '$ServiceName' not found." -ForegroundColor Red
        return 1
    }
    
    Write-Host "Service Status:" -ForegroundColor Cyan
    Write-Host "  Name:        $($service.Name)" -ForegroundColor White
    Write-Host "  DisplayName: $($service.DisplayName)" -ForegroundColor White
    Write-Host "  Status:      $($service.Status)" -ForegroundColor $(if ($service.Status -eq "Running") { "Green" } else { "Yellow" })
    Write-Host "  StartType:   $($service.StartType)" -ForegroundColor White
    
    return 0
}

# ============================================================================
# LOG VIEWING FUNCTIONS
# ============================================================================

function View-AgentCoreLogs {
    Write-Host "=== Agent Core Service Log Viewer ===" -ForegroundColor Green
    Write-Host ""
    
    # Check service status
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $service) {
        Write-Host "ERROR: Service '$ServiceName' not found!" -ForegroundColor Red
        return 1
    }
    
    Write-Host "Service Status: " -NoNewline -ForegroundColor Cyan
    $statusColor = switch ($service.Status) {
        "Running" { "Green" }
        "Stopped" { "Red" }
        "Paused" { "Yellow" }
        default { "Yellow" }
    }
    Write-Host $service.Status -ForegroundColor $statusColor
    Write-Host ""
    
    # Find log file path
    $logPath = $null
    
    # Method 1: Read from service registry environment variable
    $regPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$ServiceName"
    if (Test-Path $regPath) {
        $envString = (Get-ItemProperty -Path $regPath -Name "Environment" -ErrorAction SilentlyContinue).Environment
        if ($envString) {
            $envVars = $envString -split "`0"
            foreach ($var in $envVars) {
                if ($var -match "^AGENT_CORE_LOG_PATH=(.+)$") {
                    $logPath = $matches[1]
                    break
                }
            }
        }
    }
    
    # Method 2: Try common log locations
    if (-not $logPath -or -not (Test-Path $logPath)) {
        $scriptDir = $PSScriptRoot
        if (-not $scriptDir) {
            $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        }
        $projectRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
        $possiblePaths = @(
            (Join-Path $projectRoot "build\Release\agent-core-service.log"),
            (Join-Path $projectRoot "build\Debug\agent-core-service.log"),
            (Join-Path $projectRoot "build\agent-core-service.log"),
            "C:\logs\agent-core-service.log",
            (Join-Path $env:ProgramData "AgentCore\agent-core-service.log")
        )
        
        foreach ($path in $possiblePaths) {
            if (Test-Path $path) {
                $logPath = $path
                break
            }
        }
    }
    
    # Method 3: Try to find executable directory and check for default log
    if (-not $logPath -or -not (Test-Path $logPath)) {
        $serviceInfo = Get-WmiObject Win32_Service -Filter "Name='$ServiceName'"
        if ($serviceInfo -and $serviceInfo.PathName) {
            $binaryPath = $serviceInfo.PathName -replace '^"?(.+?)"?$', '$1'
            $exeDir = Split-Path -Parent $binaryPath
            $defaultLog = Join-Path $exeDir "agent-core-service.log"
            if (Test-Path $defaultLog) {
                $logPath = $defaultLog
            }
        }
    }
    
    # Show log file information
    if ($logPath -and (Test-Path $logPath)) {
        $logFile = Get-Item $logPath
        Write-Host "Log File: " -NoNewline -ForegroundColor Cyan
        Write-Host $logPath -ForegroundColor White
        Write-Host "Size: " -NoNewline -ForegroundColor Cyan
        Write-Host "$([math]::Round($logFile.Length / 1KB, 2)) KB" -ForegroundColor White
        Write-Host "Last Modified: " -NoNewline -ForegroundColor Cyan
        Write-Host $logFile.LastWriteTime -ForegroundColor White
        Write-Host ""
        
        # Show log content
        $separator = "=" * 80
        if ($Follow) {
            Write-Host "Following log file (Press Ctrl+C to stop)..." -ForegroundColor Yellow
            Write-Host $separator -ForegroundColor Gray
            Get-Content $logPath -Wait -Tail $Lines
        } else {
            Write-Host "Last $Lines lines of log:" -ForegroundColor Yellow
            Write-Host $separator -ForegroundColor Gray
            Get-Content $logPath -Tail $Lines
            Write-Host $separator -ForegroundColor Gray
        }
    } else {
        Write-Host "WARNING: Log file not found!" -ForegroundColor Yellow
        Write-Host "Searched locations:" -ForegroundColor Yellow
        if ($logPath) {
            Write-Host "  - $logPath" -ForegroundColor Gray
        }
        Write-Host "  - build\Release\agent-core-service.log" -ForegroundColor Gray
        Write-Host "  - build\Debug\agent-core-service.log" -ForegroundColor Gray
        Write-Host "  - build\agent-core-service.log" -ForegroundColor Gray
        Write-Host "  - C:\logs\agent-core-service.log" -ForegroundColor Gray
        Write-Host ""
        Write-Host "The service may not have started yet, or logging may be disabled." -ForegroundColor Yellow
    }
    
    # Show Windows Event Log entries
    if ($Events -or $All) {
        Write-Host ""
        Write-Host "=== Windows Event Log Entries ===" -ForegroundColor Yellow
        Write-Host ""
        
        # Check Application log for service errors
        $appEvents = Get-WinEvent -LogName Application -MaxEvents 20 -ErrorAction SilentlyContinue | 
            Where-Object { $_.ProviderName -eq $ServiceName -or $_.Message -like "*$ServiceName*" } |
            Select-Object -First 10
        
        if ($appEvents) {
            Write-Host "Application Log (last 10 entries):" -ForegroundColor Cyan
            foreach ($event in $appEvents) {
                $time = $event.TimeCreated
                $level = $event.LevelDisplayName
                $levelColor = switch ($level) {
                    "Error" { "Red" }
                    "Warning" { "Yellow" }
                    default { "White" }
                }
                Write-Host "[$time] " -NoNewline -ForegroundColor Gray
                Write-Host "[$level] " -NoNewline -ForegroundColor $levelColor
                Write-Host ($event.Message -split "`n" | Select-Object -First 1) -ForegroundColor White
            }
        } else {
            Write-Host "No recent Application log entries found for service." -ForegroundColor Gray
        }
        
        # Check System log for service control events
        $systemEvents = Get-WinEvent -LogName System -MaxEvents 20 -ErrorAction SilentlyContinue | 
            Where-Object { $_.Message -like "*$ServiceName*" } |
            Select-Object -First 10
        
        if ($systemEvents) {
            Write-Host ""
            Write-Host "System Log (last 10 entries):" -ForegroundColor Cyan
            foreach ($event in $systemEvents) {
                $time = $event.TimeCreated
                $level = $event.LevelDisplayName
                $levelColor = switch ($level) {
                    "Error" { "Red" }
                    "Warning" { "Yellow" }
                    default { "White" }
                }
                Write-Host "[$time] " -NoNewline -ForegroundColor Gray
                Write-Host "[$level] " -NoNewline -ForegroundColor $levelColor
                Write-Host ($event.Message -split "`n" | Select-Object -First 1) -ForegroundColor White
            }
        }
    }
    
    # Show additional debug log if it exists
    if ($All) {
        Write-Host ""
        Write-Host "=== Debug Log ===" -ForegroundColor Yellow
        Write-Host ""
        
        $scriptDir = $PSScriptRoot
        if (-not $scriptDir) {
            $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        }
        $projectRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
        $debugLogs = @(
            (Join-Path $projectRoot "build\Release\service-debug.log"),
            (Join-Path $projectRoot "build\Debug\service-debug.log"),
            (Join-Path $projectRoot "build\service-debug.log")
        )
        
        foreach ($debugLog in $debugLogs) {
            if (Test-Path $debugLog) {
                $separator = "=" * 80
                Write-Host "Debug Log: $debugLog" -ForegroundColor Cyan
                Write-Host $separator -ForegroundColor Gray
                Get-Content $debugLog -Tail 20
                Write-Host $separator -ForegroundColor Gray
                break
            }
        }
    }
    
    return 0
}

# ============================================================================
# MAIN
# ============================================================================

$exitCode = 0
switch ($Action) {
    "install" { $exitCode = Install-AgentCoreService }
    "uninstall" { $exitCode = Uninstall-AgentCoreService }
    "start" { $exitCode = Start-AgentCoreService }
    "stop" { $exitCode = Stop-AgentCoreService }
    "restart" { $exitCode = Restart-AgentCoreService }
    "status" { $exitCode = Get-AgentCoreServiceStatus }
    "viewlog" { $exitCode = View-AgentCoreLogs }
}

exit $exitCode
