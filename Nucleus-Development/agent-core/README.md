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
8. **Bus** - ZeroMQ IPC (PUB/SUB + REQ/REP) with message envelopes, topic filtering, and optional CURVE encryption
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
- ZeroMQ (libzmq + cppzmq for extension IPC)

**Install on Ubuntu/Debian:**
```bash
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev libzmq3-dev libcppzmq-dev
```

### Quick Start

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
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
- `zmq`: ZeroMQ bus configuration (ports, optional CURVE encryption)

### Authentication

Agent Core authenticates with the backend using X.509 certificates:

1. Certificate is loaded from the path specified in `cert.certPath`
2. HTTPS GET request is sent to `backend.baseUrl + backend.authPath + serialNumber + uuid`
3. Certificate is passed in the `ARS-ClientCert` header
4. Request includes device metadata in JSON body
5. Response 200 indicates successful authentication
6. Network errors and 5xx errors are retried according to retry policy
7. 4xx errors (client errors) are not retried

### SSM Registration

After successful authentication, Agent Core registers with AWS Systems Manager:

1. **Check backend registration**: Calls `isdeviceregistered` API to check if backend thinks device is registered
2. **Check local registration**: Verifies if SSM Agent service is running locally
3. **Get activation info**: If not registered, calls `getactivationinformation` API to get activation credentials
4. **Register with SSM**: Executes `amazon-ssm-agent -register` with the activation ID, code, and region
5. **Restart SSM service**: Restarts the SSM Agent service to apply registration

**Registration Flow:**
- If both backend and local agree device is registered → Skip registration
- If either says not registered → Get new activation info and register

**SSM Agent Path Configuration:**
- Linux (snap): `/snap/amazon-ssm-agent/current/amazon-ssm-agent`
- Linux (apt): `/usr/bin/amazon-ssm-agent`
- Windows: Path to `amazon-ssm-agent.exe`

**Note:** SSM registration requires root/administrator privileges.

**Example Configuration:**
```json
{
  "backend": {
    "baseUrl": "https://35.159.104.91:443",
    "authPath": "/deviceservices/api/Authentication/devicecertificatevalid/",
    "isRegisteredPath": "/deviceservices/api/devicemanagement/isdeviceregistered/",
    "getActivationPath": "/deviceservices/api/devicemanagement/getactivationinformation/"
  },
  "identity": {
    "deviceSerial": "200000",
    "uuid": "a1635025-2723-4ffa-b608-208578d6128f"
  },
  "cert": {
    "certPath": "./cert_base64(200000).txt"
  },
  "retry": {
    "maxAttempts": 5,
    "baseMs": 500,
    "maxMs": 8000
  },
  "ssm": {
    "agentPath": "/snap/amazon-ssm-agent/current/amazon-ssm-agent"
  },
  "zmq": {
    "pubPort": 5555,
    "reqPort": 5556,
    "curveEnabled": false,
    "curveServerKey": "",
    "curvePublicKey": "",
    "curveSecretKey": ""
  }
}
```

### Service Manager

Agent Core automatically installs and manages itself as an OS service:

1. **Check if installed**: On startup, checks if systemd/Windows service is installed
2. **Self-install**: If not installed, installs itself as a service automatically
3. **Start service**: Starts the service and exits the installer process
4. **Restart management**: Handles catastrophic failures with exponential backoff and quarantine
5. **Graceful shutdown**: Responds to SIGTERM/SIGINT, stops extensions cleanly

**Installation Flow:**
- First run detects service is not installed
- Copies binary to system location (`/usr/local/bin` on Linux)
- Creates service definition (systemd unit or Windows SCM service)
- Enables service to start on boot
- Starts service immediately
- Installer process exits (service continues running)

**Restart and Quarantine:**
- Process crashes trigger OS-level restart (systemd `Restart=on-failure`)
- Agent loads restart state from `/var/lib/agent-core/restart-state.json`
- Applies exponential backoff delay before initialization
- Tracks restart count across crashes
- After max attempts (default: 5), enters quarantine for 1 hour
- Successful stable runtime (5 minutes) resets restart counter

**Note:** Installation requires root/administrator privileges.

**Install (First Run):**
```bash
# Linux
sudo ./agent-core --config /path/to/config.json

# Windows (Command Prompt as Administrator)
agent-core.exe --config C:\path\to\config.json
```

**Start:**
```bash
# Linux
sudo systemctl start agent-core

# Windows
sc start AgentCore
```

**Stop:**
```bash
# Linux
sudo systemctl stop agent-core

# Windows
sc stop AgentCore
```

**Restart:**
```bash
# Linux
sudo systemctl restart agent-core

# Windows
sc stop AgentCore && sc start AgentCore
```

**View Logs:**
```bash
# Linux
sudo journalctl -u agent-core -f

# Windows (Event Viewer)
eventvwr.msc
```

## Extensions

Extensions are separate executables launched from `manifests/extensions.json`. They communicate over ZeroMQ using versioned message envelopes with headers, authentication context, and topic-based routing.

### ZeroMQ Bus Features

- **Message Envelopes**: Versioned message format (v1, v2) with backward compatibility
- **Headers**: Key-value metadata for message routing and processing hints
- **Auth Context**: Device identity and certificate information in every message
- **Topic Filtering**: Support for exact match, prefix patterns (`ext.ps.`), and wildcards (`ext.ps.*`)
- **Correlation IDs**: Request/response correlation with automatic ID preservation
- **CURVE Encryption**: Optional end-to-end encryption for inter-process communication (configurable)
- **Cross-Platform Transport**: 
  - **Linux**: Uses IPC sockets (`ipc:///tmp/agent-bus-*`) for efficient in-process communication
  - **Windows**: Uses TCP localhost (`tcp://127.0.0.1:port`) due to ZeroMQ IPC limitations on Windows
  - **CURVE Encryption**: Only applied to TCP connections (Windows or when explicitly enabled)

### Extension Contract (ZeroMQ)

#### Envelope Structure (Version 2)

```json
{
  "v": 2,
  "topic": "ext.ps.exec.req",
  "correlationId": "550e8400-e29b-41d4-a716-446655440000",
  "payload": {
    "action": "ExecuteScript",
    "script": "Get-Process",
    "timeout": 30
  },
  "ts": 1731283200000,
  "headers": {
    "source": "agent-core",
    "priority": "normal"
  },
  "authContext": {
    "deviceSerial": "SN123456",
    "gatewayId": "",
    "uuid": "device-uuid-123",
    "certValid": true,
    "certExpiresMs": 1733875200000
  }
}
```

#### Version 1 (Legacy - Still Supported)

```json
{
  "v": 1,
  "topic": "ext.tunnel.start",
  "correlationId": "uuid",
  "payload": { "mode": "vpn" },
  "ts": 1731283200000
}
```

#### Topic Subscription Patterns

- **Exact match**: `"ext.ps.exec.req"` - matches only this exact topic
- **Prefix match**: `"ext.ps."` - matches all topics starting with this prefix
- **Wildcard**: `"ext.ps.*"` - matches all topics starting with `ext.ps.`

#### Example: Tunnel Extension Response
```json
{
  "v": 2,
  "topic": "ext.tunnel.ready",
  "correlationId": "550e8400-e29b-41d4-a716-446655440000",
  "payload": {
  "event": "TunnelReady",
  "details": {
    "path": "vpn",
    "resolver": "unbound",
    "routes": ["10.0.0.0/16"]
    }
  },
  "ts": 1731283200100,
  "headers": {
    "source": "tunnel-extension"
  },
  "authContext": {
    "deviceSerial": "SN123456",
    "uuid": "device-uuid-123",
    "certValid": true
  }
}
```

For detailed schema documentation, see `docs/envelope_schema.md`.

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
- `test_zmq_load` - ZeroMQ load tests (10k msgs/min, PUB/SUB, REQ/REP, soak test)
- `test_ssm_registration` - SSM registration integration tests (some tests require sudo)

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

### ZeroMQ Load Test

The ZeroMQ load test verifies message throughput and reliability:

```bash
# Run the load test
./build/tests/test_zmq_load
```

The test validates:
1. **PUB/SUB Load Test**: 10k messages/minute with zero loss verification
2. **REQ/REP Load Test**: Request/response pattern with correlation ID matching
3. **Soak Test**: Extended 60-second run to verify stability

**Test Results:**
- Target: 10k messages/minute (~167 msgs/sec)
- Verifies: Zero message loss, latency measurements, throughput validation

**Prerequisites:**
- Sample extension must be built at `extensions/sample/build/sample-ext`
- ZeroMQ must be installed and available

**ZeroMQ Configuration:**

The ZeroMQ bus can be configured in the `zmq` section of the config file:

```json
{
  "zmq": {
    "pubPort": 5555,        // PUB/SUB port (default: 5555)
    "reqPort": 5556,        // REQ/REP port (default: 5556)
    "curveEnabled": false,  // Enable CURVE encryption for inter-process (TCP) connections
    "curveServerKey": "",    // Server public key (40 chars base64) - required if curveEnabled=true
    "curvePublicKey": "",    // Client public key (40 chars base64) - required if curveEnabled=true
    "curveSecretKey": ""     // Client secret key (40 chars base64) - required if curveEnabled=true
  }
}
```

**Platform-Specific Transport:**
- **Linux**: Uses IPC sockets (`ipc:///tmp/agent-bus-pub`, `ipc:///tmp/agent-bus-req`) for efficient local communication. IPC sockets are automatically cleaned up when the process exits. Port numbers are ignored on Linux (IPC paths are used instead).
- **Windows**: Uses TCP localhost (`tcp://127.0.0.1:pubPort`, `tcp://127.0.0.1:reqPort`) because ZeroMQ IPC doesn't work reliably on Windows. Port numbers from config are used.
- **CURVE Encryption**: Only applied to TCP connections (Windows or when explicitly enabled). IPC connections on Linux do not use CURVE encryption as they are already local-only and more efficient.

### SSM Registration Integration Test

The SSM registration integration test verifies the registration flow with AWS Systems Manager:

```bash
# Run tests that don't require sudo (backend checks, error handling)
./build/tests/test_ssm_registration

# Run all tests including full SSM registration (requires sudo)
sudo ./build/tests/test_ssm_registration --full
```

The test will verify:
1. Backend registration status check
2. Local SSM agent status check
3. Activation info retrieval from backend
4. Error handling for invalid serial numbers, certificates, and backend URLs
5. Empty activation info validation
6. Full SSM registration flow (with `--full` flag)

**Test Categories:**
- **Without sudo**: Tests API communication, error handling, and validation logic
- **With sudo**: Tests actual SSM agent registration (requires root privileges)

**Prerequisites:**
- Network connectivity to backend API
- Valid certificate file at `cert_base64(200000).txt`
- AWS SSM Agent installed (for full registration test)
- Backend API accessible at configured URL

### Chaos Testing
```bash
# Simulate network failures, crashes, etc.
./scripts/fault-inject.sh
```

