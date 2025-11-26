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
- libcurl (for HTTPS communication)
- nlohmann/json (for JSON parsing)
- ZeroMQ (for extension IPC, future)
- OpenSSL (for TLS, future)

### Quick Start

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build buildient_(create_https_client()) {

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
- `backend`: API endpoint and authentication path
- `identity`: Device/Gateway identification (serial number, UUID)
- `tunnelInfo`: Tunnel extension control
- `mqtt`: MQTT broker settings
- `cert`: Certificate management (path to certificate file)
- `retry`: Backoff and circuit breaker (max attempts, delays)
- `resource`: CPU/Memory/Network budgets
- `logging`: Log level and format

### Authentication

Agent Core authenticates with the backend using X.509 certificates:

1. Certificate is loaded from the path specified in `cert.certPath`
2. HTTPS GET request is sent to `backend.baseUrl + backend.authPath + serialNumber + uuid`
3. Certificate is passed in the `ARS-ClientCert` header
4. Request includes device metadata in JSON body
5. Response 200 indicates successful authentication
6. Network errors and 5xx errors are retried according to retry policy
7. 4xx errors (client errors) are not retried

**Example Configuration:**
```json
{
  "backend": {
    "baseUrl": "https://35.159.104.91:443",
    "authPath": "/deviceservices/api/Authentication/devicecertificatevalid/"
  },
  "identity": {
    "deviceSerial": "200000",
    "uuid": "a1635025-2723-4ffa-b608-208578d6128f"
  },
  "cert": {
    "certPath": "../cert_base64(200000).txt"
  },
  "retry": {
    "maxAttempts": 5,
    "baseMs": 500,
    "maxMs": 8000
  }
}
```

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
4. Add integration tests in `tests/integration/`

## Testing

### Build with Tests Enabled

```bash
# Configure with testing enabled
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

# Build all targets including tests
cmake --build build
```

### Run Tests

```bash
# Run all tests using CTest
ctest --test-dir build --output-on-failure

# Or run a specific test directly
./build/tests/test_auth  # Authentication integration tests
./build/tests/test_zmq   # ZeroMQ bus integration tests
```

**Available Tests:**
- `test_auth` - Authentication integration tests (requires network connectivity and certificate file)
- `test_zmq` - ZeroMQ bus integration tests (requires sample extension to be built)

### ZeroMQ Integration Test

The ZeroMQ integration test verifies the bus communication between agent-core and extensions:

```bash
# First, ensure the sample extension is built
cd ../extensions/sample
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd ../../agent-core

# Run the ZeroMQ integration test
./build/tests/test_zmq
```

The test will:
1. Launch the sample extension automatically
2. Send a request via ZeroMQ REQ/REP pattern
3. Verify the reply and correlation ID round trip
4. Test correlation ID preservation
5. Clean up by stopping the extension

**Prerequisites:**
- Sample extension must be built at `extensions/sample/build/sample-ext`
- ZeroMQ must be installed and available

### Chaos Testing
```bash
# Simulate network failures, crashes, etc.
./scripts/fault-inject.sh
```

