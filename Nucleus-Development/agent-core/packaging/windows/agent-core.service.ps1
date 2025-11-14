# PowerShell script to install/uninstall Agent Core as Windows Service

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("install", "uninstall", "start", "stop")]
    [string]$Action
)

$ServiceName = "AgentCore"
$DisplayName = "Agent Core IoT Service"
$Description = "Cross-platform IoT Agent Core service for device management"
$BinaryPath = "$PSScriptRoot\..\..\build\agent-core.exe --config $PSScriptRoot\..\..\config\prod.json"

function Install-AgentCoreService {
    Write-Host "Installing Agent Core service..." -ForegroundColor Green
    
    # Check if service already exists
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        Write-Host "Service already exists. Uninstall first." -ForegroundColor Yellow
        return
    }
    
    # Create service
    New-Service -Name $ServiceName `
                -BinaryPathName $BinaryPath `
                -DisplayName $DisplayName `
                -Description $Description `
                -StartupType Automatic
    
    Write-Host "Service installed successfully!" -ForegroundColor Green
}

function Uninstall-AgentCoreService {
    Write-Host "Uninstalling Agent Core service..." -ForegroundColor Green
    
    # Stop if running
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($service) {
        if ($service.Status -eq "Running") {
            Stop-Service -Name $ServiceName
            Write-Host "Service stopped" -ForegroundColor Yellow
        }
        
        # Remove service
        sc.exe delete $ServiceName
        Write-Host "Service uninstalled successfully!" -ForegroundColor Green
    } else {
        Write-Host "Service not found" -ForegroundColor Yellow
    }
}

function Start-AgentCoreService {
    Write-Host "Starting Agent Core service..." -ForegroundColor Green
    Start-Service -Name $ServiceName
    Write-Host "Service started!" -ForegroundColor Green
}

function Stop-AgentCoreService {
    Write-Host "Stopping Agent Core service..." -ForegroundColor Green
    Stop-Service -Name $ServiceName
    Write-Host "Service stopped!" -ForegroundColor Green
}

# Main
switch ($Action) {
    "install" { Install-AgentCoreService }
    "uninstall" { Uninstall-AgentCoreService }
    "start" { Start-AgentCoreService }
    "stop" { Stop-AgentCoreService }
}
