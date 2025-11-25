# Agent Core Architecture

## Executive Summary

Agent Core is a cross-platform C++ IoT service that manages device/gateway identity, secure connectivity, command routing, and extension orchestration. It runs as a Windows Service or Linux systemd daemon with high reliability, offline capability, and self-healing.

## Design Principles

### 1. Simple Processes, Strong Contracts
- Agent Core runs as main process
- Extensions run as separate executables
- Communication via ZeroMQ with versioned message envelopes
- Clear boundaries prevent cascading failures

### 2. RAII + PIMPL
- Resource Acquisition Is Initialization for deterministic cleanup
- Pointer to Implementation for ABI stability
- Long-lived daemon requires stable interfaces

### 3. Dependency Injection
- Constructor injection of dependencies
- Interface-based subsystems
- Testable without mocking frameworks

### 4. Fail Fast at Edges, Not at Core
- Authentication and tunnels can fail independently
- Core remains alive with backoff/retry
- Circuit breakers prevent resource exhaustion

### 5. Observability First
- Structured logging with correlation IDs
- Metrics (counters, histograms, gauges)
- Health endpoints for monitoring

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Agent Core                            │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ ServiceHost │  │    Config    │  │   Identity   │       │
│  └─────────────┘  └──────────────┘  └──────────────┘       │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ AuthManager  │  │ Registration │  │ MqttClient   │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ ExtManager   │  │     Bus      │  │   Telemetry  │     │
│  │              │  │  (ZeroMQ)    │  │              │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
└──────────────────────────┬───────────────────────────────────┘
                           │ ZeroMQ IPC
          ┌────────────────┼────────────────┐
          │                │                │
     ┌────▼────┐      ┌────▼────┐     ┌────▼────┐
     │  Tunnel │      │   PS    │     │ Custom  │
     │   Ext   │      │   Ext   │     │   Ext   │
     └─────────┘      └─────────┘     └─────────┘
```

## State Machine

The agent follows a deterministic startup sequence:

```
INIT
  │
  ├─→ LOAD_CONFIG (read JSON, validate schema)
  │
  ├─→ IDENTITY_RESOLVE (device vs gateway, discover serial/ID)
  │
  ├─→ NET_DECIDE (direct HTTPS vs tunnel)
  │    │
  │    └─→ if tunnel: StartTunnel → wait for TunnelReady
  │
  ├─→ AUTH (check/renew X.509 cert bound to identity)
  │
  ├─→ REGISTER (SSM MVP, future: IoT/MQTT)
  │
  ├─→ MQTT_CONNECT (mTLS, subscribe to command topics)
  │
  ├─→ RUNLOOP
  │    │
  │    ├─→ Process MQTT commands
  │    ├─→ Route to extensions via Bus
  │    ├─→ Monitor resource usage
  │    ├─→ Health checks
  │    ├─→ Retry failed operations
  │    └─→ Emit telemetry
  │
  └─→ SHUTDOWN (graceful stop, flush logs)
```

## Core Subsystems

### ServiceHost
**Responsibility**: OS integration (Windows Service, Linux systemd)

**Key Functions**:
- Initialize as service/daemon
- Handle OS signals (SIGTERM, SIGINT, SCM stop)
- Provide main event loop
- Coordinate graceful shutdown

**Platform Variants**:
- `ServiceHostWin`: Windows SCM integration
- `ServiceHostLinux`: systemd with signal handling

### Config
**Responsibility**: Configuration loading and validation

**Key Functions**:
- Load JSON configuration
- Validate schema
- Provide type-safe access
- Live reconfiguration (future)

**Schema Sections**:
- Backend (API endpoints)
- Identity (device/gateway)
- Tunnel (enable/disable)
- MQTT (broker, ports, keepalive)
- Certificates (store, renewal)
- Retry (backoff parameters)
- Resources (CPU/mem/net budgets)
- Logging (level, format)

### Identity
**Responsibility**: Determine device vs gateway identity

**Key Functions**:
- Read from config (override)
- Discover from system (hostname, machine-id)
- Validate uniqueness
- Bind to certificates

**Identity Types**:
- **Device**: Single endpoint (serial number)
- **Gateway**: Aggregator for multiple devices (gateway ID)

### NetPathSelector
**Responsibility**: Choose network path (direct vs tunnel)

**Key Functions**:
- Evaluate config (tunnel enabled?)
- Check network conditions (future)
- Return decision with reason

**Decision Logic**:
```cpp
if (config.tunnel.enabled) {
    return Path::Tunnel;
} else {
    return Path::Direct;
}
```

**Note**: Core only decides; tunnel extension handles VPN/IPsec

### AuthManager
**Responsibility**: X.509 certificate lifecycle

**Key Functions**:
- Check certificate validity
- Renew if < N days remaining
- Bind to device/gateway identity
- Store in OS keystore

**States**:
- `Valid`: Certificate OK
- `Renewed`: Certificate renewed successfully
- `Failed`: Certificate unavailable or invalid

### Registration
**Responsibility**: Register device/gateway with backend

**MVP Implementation**: AWS SSM
- Store device/gateway metadata
- Establish trust relationship
- Return activation token

**Future**: IoT Core / MQTT-based registration

### MqttClient
**Responsibility**: Secure MQTT connection

**Key Functions**:
- Connect with mTLS (X.509)
- Subscribe to command topics
- Publish responses
- Handle reconnection

**Topics**:
- Commands: `device/{serial}/commands`
- Responses: `device/{serial}/responses`
- Heartbeat: `device/{serial}/heartbeat`

### Bus (ZeroMQ)
**Responsibility**: IPC message bus for extensions

**Patterns**:
- **PUB/SUB**: Core publishes events, extensions subscribe
- **REQ/REP**: Extensions request actions, core replies

**Envelope Format**:
```json
{
  "v": 1,
  "action": "StartTunnel",
  "params": { "mode": "vpn" },
  "correlationId": "uuid",
  "requestedBy": "agent-core",
  "ts": 1731283200000
}
```

### ExtensionManager
**Responsibility**: Launch, monitor, restart extensions

**Key Functions**:
- Load manifest (JSON spec)
- Launch processes
- Monitor health (heartbeat, exit codes)
- Restart on crash (with backoff)
- Quarantine after N failures

**Extension States**:
- `Starting`: Process launching
- `Running`: Healthy
- `Crashed`: Unexpected exit
- `Quarantined`: Too many crashes
- `Stopped`: Graceful shutdown

### ResourceMonitor
**Responsibility**: Enforce CPU/memory/network budgets

**Key Functions**:
- Sample resource usage per process
- Compare against budgets
- Emit warnings
- Throttle or stop violators

**Budgets (from config)**:
- CPU: max percentage
- Memory: max MB
- Network: max KB/s

### Telemetry
**Responsibility**: Structured logs and metrics

**Logger**:
- Levels: Trace, Debug, Info, Warn, Error, Critical
- Structured fields (subsystem, deviceId, correlationId)
- JSON or text format

**Metrics**:
- **Counters**: retry attempts, commands received
- **Histograms**: latency distributions
- **Gauges**: current CPU/mem/net usage

### RetryPolicy
**Responsibility**: Exponential backoff with circuit breaker

**Key Functions**:
- Execute operation with retries
- Capped exponential backoff
- Jitter (±20%) to prevent thundering herd
- Circuit breaker (open after N failures)

**States**:
- `Closed`: Normal operation
- `Open`: Fast-fail, too many errors
- `HalfOpen`: Testing recovery

## Message Contracts

### Core → Extension

#### StartTunnel
```json
{
  "v": 1,
  "action": "StartTunnel",
  "params": { "mode": "vpn" },
  "correlationId": "uuid",
  "requestedBy": "agent-core",
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
  "requestedBy": "agent-core",
  "ts": 1731283200000
}
```

#### PowerShellExecute
```json
{
  "v": 1,
  "action": "PowerShellExecute",
  "name": "CollectDiskUsage",
  "script": "Get-PSDrive",
  "timeoutMs": 20000,
  "correlationId": "uuid"
}
```

### Extension → Core

#### TunnelReady
```json
{
  "v": 1,
  "event": "TunnelReady",
  "details": {
    "path": "vpn",
    "resolver": "unbound",
    "routes": ["10.0.0.0/16"]
  },
  "correlationId": "uuid"
}
```

#### TunnelFailed
```json
{
  "v": 1,
  "event": "TunnelFailed",
  "error": "VPN connection timeout",
  "correlationId": "uuid"
}
```

#### ScriptResult
```json
{
  "v": 1,
  "event": "ScriptResult",
  "exitCode": 0,
  "stdout": "...",
  "stderr": "",
  "correlationId": "uuid"
}
```

## Security Posture

### Authentication
- mTLS everywhere (device/gateway cert)
- Private keys in OS keystores
- Configurable renewal (default: 30 days)

### Authorization
- Device/gateway-scoped topics
- Command validation before routing
- Extension sandboxing (future)

### Data Protection
- Secrets redacted in logs
- TLS 1.2+ for all network traffic
- Certificate pinning (future)

### Audit Trail
- Structured security events
- Correlation IDs for forensics
- Immutable log shipping

## Performance Characteristics

### Resource Footprint
- Core: ~50 MB memory, <5% CPU idle
- Extensions: varies by workload
- Total budget: 512 MB, 60% CPU (configurable)

### Latency
- MQTT publish: <10ms (LAN)
- Extension IPC: <5ms (ZeroMQ)
- Command routing: <20ms end-to-end

### Scalability
- Extensions: up to 10 concurrent
- MQTT subscriptions: up to 100 topics
- Heartbeat: 30s (configurable)

## Error Handling

### Categories
1. **Transient**: Network glitches, temporary unavailability
2. **Persistent**: Configuration errors, auth failures
3. **Fatal**: Out of memory, corrupted state

### Strategies
- **Transient**: Retry with backoff
- **Persistent**: Log, emit metric, continue with degraded functionality
- **Fatal**: Log, flush, exit with code

### Circuit Breaker
- Open after 10 consecutive failures
- Half-open after 60s
- Close after 3 successes

## Testing Strategy

### Unit Tests
- Pure interfaces with mock implementations
- Deterministic time (injected clock)
- Fast (<1ms per test)

### Integration Tests
- In-process ZeroMQ
- Fake MQTT broker
- Echo extension stubs

### Chaos Tests
- Drop MQTT connections
- Flap DNS
- Corrupt cert store
- Kill extensions
- Assert recovery/quarantine

### Platform Tests
- Windows 10/11
- Ubuntu 22.04
- Parity smoke tests

## Deployment

### Package Contents
- `agent-core` binary
- Configuration files
- Extension binaries
- Service install scripts

### Installation
**Linux**:
```bash
sudo dpkg -i agent-core_0.1.0_amd64.deb
sudo systemctl enable agent-core
sudo systemctl start agent-core
```

**Windows**:
```powershell
agent-core-installer.exe /silent
sc start AgentCore
```

### Upgrades
- Stop service
- Replace binaries
- Migrate config (if schema changed)
- Restart service

### Rollback
- Stop service
- Restore previous binaries
- Restart service

## References

- [ZeroMQ Guide](https://zguide.zeromq.org/)
- [MQTT Specification](https://mqtt.org/mqtt-specification/)
- [AWS IoT Core](https://aws.amazon.com/iot-core/)
- [systemd Service](https://www.freedesktop.org/software/systemd/man/systemd.service.html)
- [Windows Services](https://docs.microsoft.com/en-us/windows/win32/services/services)
