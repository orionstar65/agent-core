# Agent Core (C++)

## Overview

A cross-platform C++ IoT service that manages identity, connectivity, authentication, registration, MQTT command channel, and orchestration of extensions via ZeroMQ.

## Features

- **Cross-Platform**: Runs as Windows Service or Linux systemd daemon
- **Resilient**: Graceful start/stop with offline operation and self-healing
- **Network Flexibility**: Direct HTTPS or tunnel routing (tunnel managed by extension)
- **Secure**: X.509 certificate bootstrap/renewals bound to device or gateway identity
- **MQTT Integration**: Command channel with QoS guarantees
- **Extension Architecture**: Manages external processes via ZeroMQ message bus
- **Resource Management**: CPU/Memory/Network budget enforcement
- **Observable**: Structured logs and metrics with correlation IDs

## Architecture

### Processes
- `agent-core` - Main service/daemon
- Extension processes (e.g., `ext-tunnel`, `ext-ps`)

### Core Subsystems
1. **ServiceHost** - OS integration (Windows SCM / Linux systemd)
2. **Config** - JSON configuration with validation
3. **Identity** - Device vs gateway ID resolution
4. **NetPathSelector** - Direct vs tunnel decision logic
5. **AuthManager** - X.509 certificate lifecycle
6. **Registration** - Backend registration (MVP: AWS SSM)
7. **MqttClient** - Secure MQTT connection
8. **Bus** - ZeroMQ IPC (PUB/SUB + REQ/REP)
9. **ExtensionManager** - Process lifecycle and supervision
10. **ResourceMonitor** - Budget enforcement
11. **Telemetry** - Structured logging and metrics
12. **RetryPolicy** - Exponential backoff with circuit breaker

## Build

### Prerequisites
- CMake 3.15 or higher
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- (Future) ZeroMQ, OpenSSL, nlohmann/json

### Quick Start

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests (when implemented)
ctest --test-dir build
```

### Build Variants

```bash
# Debug build
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Release with tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
```

## Running

### Development Mode

```bash
./build/agent-core --config ./config/dev.json
```

### Configuration

See `config/example.json` for full configuration schema.

Key configuration sections:
- `backend`: API endpoint
- `identity`: Device/Gateway identification
- `tunnelInfo`: Tunnel extension control
- `mqtt`: MQTT broker settings
- `cert`: Certificate management
- `retry`: Backoff and circuit breaker
- `resource`: CPU/Memory/Network budgets
- `logging`: Log level and format

### As a Service

**Linux (systemd):**
```bash
sudo cp packaging/linux/agent-core.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable agent-core
sudo systemctl start agent-core
```

**Windows (SCM):**
```powershell
# See packaging/windows/agent-core.service.ps1
```

## Extensions

Extensions are separate executables launched from `manifests/extensions.json`. They communicate over ZeroMQ using the common Envelope contract.

### Extension Contract (ZeroMQ)

#### Envelope Structure
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

#### Example: Tunnel Extension
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

## State Machine

The agent follows this startup sequence:

1. **INIT** → Initialize subsystems
2. **LOAD_CONFIG** → Parse configuration
3. **IDENTITY_RESOLVE** → Discover device/gateway ID
4. **NET_DECIDE** → Choose direct or tunnel path
5. **AUTH** → Ensure certificate validity
6. **REGISTER** → Register with backend (SSM MVP)
7. **MQTT_CONNECT** → Establish MQTT session
8. **RUNLOOP** → Process commands, monitor health
9. **SHUTDOWN** → Graceful cleanup

## Logging & Metrics

### Structured Logs
- Timestamp, level, subsystem, deviceId, correlationId
- JSON format for machine parsing
- Configurable log level

### Metrics
- **Counters**: retry attempts, commands received, heartbeats
- **Histograms**: latency distributions
- **Gauges**: CPU/memory/network usage per process

## Development

### Project Structure
```
agent-core/
├── include/agent/        # Public headers (PIMPL interfaces)
├── src/                  # Implementation
│   ├── service/         # Platform-specific service hosts
│   ├── config/          # Configuration loading
│   ├── identity/        # Identity discovery
│   ├── net/             # Network path selection
│   ├── auth/            # Authentication/certificates
│   ├── reg/             # Registration
│   ├── mqtt/            # MQTT client
│   ├── bus/             # ZeroMQ bus
│   ├── ext/             # Extension manager
│   ├── res/             # Resource monitor
│   ├── util/            # Utilities
│   └── telemetry/       # Logging/metrics
├── extensions/          # Extension projects
├── manifests/           # Extension launch configs
├── config/              # Configuration files
├── tests/               # Unit and integration tests
└── packaging/           # Service install scripts
```

### Design Patterns
- **RAII**: Deterministic resource cleanup
- **PIMPL**: ABI-stable interfaces
- **Dependency Injection**: Constructor-injected dependencies
- **Interface Segregation**: Small, focused interfaces

### Adding a New Subsystem
1. Define interface in `include/agent/`
2. Implement in `src/`
3. Register in `main.cpp` dependency graph
4. Add unit tests in `tests/unit/`

## Testing

### Unit Tests
```bash
cmake --build build --target unit_tests
./build/tests/unit_tests
```

### Integration Tests
```bash
cmake --build build --target integration_tests
./build/tests/integration_tests
```

### Chaos Testing
```bash
# Simulate network failures, crashes, etc.
./scripts/fault-inject.sh
```

