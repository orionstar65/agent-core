# PowerShell Execution Extension

## Overview

The PowerShell Execution Extension executes PowerShell scripts on behalf of agent-core. It runs as a separate process and communicates via ZeroMQ.

## Responsibilities

- Execute PowerShell scripts with timeout enforcement
- Capture stdout and stderr
- Return exit codes and output to agent-core
- Handle script cancellation
- Sandbox execution (future)

## Communication Protocol

### Messages from Agent Core

#### PowerShellExecute
```json
{
  "v": 1,
  "action": "PowerShellExecute",
  "name": "CollectDiskUsage",
  "script": "Get-PSDrive -PSProvider FileSystem | Select Name,Free,Used",
  "timeoutMs": 20000,
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

#### CancelExecution
```json
{
  "v": 1,
  "action": "CancelExecution",
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

### Messages to Agent Core

#### ScriptResult
```json
{
  "v": 1,
  "event": "ScriptResult",
  "name": "CollectDiskUsage",
  "exitCode": 0,
  "stdout": "Name      Free        Used\nC      50000000  100000000",
  "stderr": "",
  "durationMs": 1234,
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

#### ScriptFailed
```json
{
  "v": 1,
  "event": "ScriptFailed",
  "name": "CollectDiskUsage",
  "error": "Execution timeout exceeded",
  "exitCode": -1,
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

## Building

```bash
cd extensions/ps-exec
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Running Standalone (for testing)

```bash
./build/ext-ps
```

## Deployment

The PS-Exec extension is deployed alongside agent-core but runs as a separate process:

**Linux**:
```bash
sudo cp build/ext-ps /usr/local/bin/
```

**Windows**:
```powershell
copy build\Release\ext-ps.exe C:\Program Files\AgentCore\
```

## Platform Support

- **Windows**: Native PowerShell execution
- **Linux**: PowerShell Core (pwsh) if installed

## Security Considerations

- Script execution sandboxing (future)
- Resource limits (CPU, memory)
- Timeout enforcement
- No elevation by default
- Audit logging for all executed scripts

## Example Use Cases

1. **System Information Collection**
   ```powershell
   Get-ComputerInfo | Select OSName, OSVersion, TotalPhysicalMemory
   ```

2. **Disk Usage Monitoring**
   ```powershell
   Get-PSDrive -PSProvider FileSystem | Select Name, @{N='Free(GB)';E={$_.Free/1GB}}, @{N='Used(GB)';E={$_.Used/1GB}}
   ```

3. **Service Status Check**
   ```powershell
   Get-Service -Name 'AgentCore' | Select Status, StartType
   ```

4. **Event Log Query**
   ```powershell
   Get-EventLog -LogName System -Newest 10 -EntryType Error
   ```
