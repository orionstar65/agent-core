# Tunnel Extension

## Overview

The Tunnel Extension manages VPN/IPsec tunnel connections for the Agent Core. It runs as a separate process and communicates with agent-core via ZeroMQ.

## Responsibilities

- Establish VPN or IPsec tunnels on demand
- Configure local DNS resolver (e.g., unbound) to use tunnel
- Manage tunnel lifecycle (connect, monitor, reconnect)
- Report tunnel status to agent-core
- Handle graceful shutdown

## Communication Protocol

### Messages from Agent Core

#### StartTunnel
```json
{
  "v": 1,
  "action": "StartTunnel",
  "params": { 
    "mode": "vpn",
    "server": "vpn.example.com",
    "credentials": "..."
  },
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

#### StopTunnel
```json
{
  "v": 1,
  "action": "StopTunnel",
  "params": {},
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

#### GetTunnelStatus
```json
{
  "v": 1,
  "action": "GetTunnelStatus",
  "params": {},
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

### Messages to Agent Core

#### TunnelReady
```json
{
  "v": 1,
  "event": "TunnelReady",
  "details": {
    "path": "vpn",
    "resolver": "unbound",
    "routes": ["10.0.0.0/16"],
    "localIp": "10.0.1.5"
  },
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

#### TunnelFailed
```json
{
  "v": 1,
  "event": "TunnelFailed",
  "error": "VPN connection timeout",
  "details": {
    "exitCode": 1,
    "stderr": "..."
  },
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

#### TunnelStatus
```json
{
  "v": 1,
  "event": "TunnelStatus",
  "status": {
    "connected": true,
    "uptime": 3600,
    "bytesIn": 1048576,
    "bytesOut": 524288
  },
  "correlationId": "uuid",
  "ts": 1731283200000
}
```

## Building

```bash
cd extensions/tunnel
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Running Standalone (for testing)

```bash
./build/ext-tunnel --config ./config/tunnel.json
```

## Deployment

The tunnel extension is deployed alongside agent-core but runs as a separate process:

**Linux**:
```bash
sudo cp build/ext-tunnel /usr/local/bin/
```

**Windows**:
```powershell
copy build\Release\ext-tunnel.exe C:\Program Files\AgentCore\
```

## Configuration

Example `config/tunnel.json`:
```json
{
  "mode": "vpn",
  "reconnectAttempts": 5,
  "reconnectDelay": 5000,
  "healthCheckInterval": 30000
}
```

## Dependencies

- ZeroMQ (for IPC with agent-core)
- Platform-specific VPN client (OpenVPN, WireGuard, etc.)
- DNS resolver (optional: unbound)

## Platform Support

- **Linux**: OpenVPN, WireGuard, IPsec (strongSwan)
- **Windows**: OpenVPN, WireGuard, native VPN client

## Security Considerations

- Credentials stored securely (OS keystore)
- Tunnel traffic isolated from host network
- Firewall rules to prevent leakage
- Certificate validation for VPN servers
