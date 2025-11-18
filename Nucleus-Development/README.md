# Nucleus IoT Platform

## Overview

Nucleus is a cross-platform IoT platform consisting of:
- **Agent Core**: Main service/daemon for device management
- **Extensions**: Independent executable modules for specific functionality

## Project Structure

```
Nucleus-Development/
├── agent-core/              # Main agent service
│   ├── include/agent/       # Public headers
│   ├── src/                 # Implementation
│   ├── config/              # Configuration files
│   ├── manifests/           # Extension manifests
│   ├── packaging/           # Service installation scripts
│   ├── scripts/             # Build and development scripts
│   └── tests/               # Unit and integration tests
│
└── extensions/              # Independent extension projects
    ├── tunnel/              # VPN/IPsec tunnel management
    ├── ps-exec/             # PowerShell script execution
    └── sample/              # Example extension for reference
```

## Design Philosophy

### Independent Extensions

Extensions are **separate projects** that can be:
- Developed independently
- Built separately
- Versioned independently
- Deployed selectively
- Updated without affecting agent-core

### Communication

- **Agent Core** ↔ **Extensions**: ZeroMQ IPC (PUB/SUB + REQ/REP)
- **Agent Core** ↔ **Backend**: MQTT over mTLS
- All communication uses versioned JSON envelopes

## Quick Start

### Building Agent Core

```bash
cd agent-core
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Building Extensions

Each extension is built independently:

```bash
# Tunnel Extension
cd extensions/tunnel
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# PS-Exec Extension
cd extensions/ps-exec
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Running Agent Core

```bash
cd agent-core
./build/agent-core --config ./config/dev.json
```

## Extension Development

### Creating a New Extension

1. Create a new directory under `extensions/`
2. Add CMakeLists.txt with executable target
3. Implement ZeroMQ communication with agent-core
4. Follow the message envelope contract (see ARCHITECTURE.md)
5. Build and test independently

### Extension Template Structure

```
extensions/my-extension/
├── CMakeLists.txt
├── README.md
├── src/
│   └── main.cpp
├── include/ (if needed)
└── config/ (if needed)
```

### Message Envelope Contract

All extensions must use this JSON envelope format:

```json
{
  "v": 1,
  "action": "ActionName",
  "params": { ... },
  "correlationId": "uuid",
  "requestedBy": "agent-core",
  "ts": 1731283200000
}
```

## Deployment Scenarios

### All-in-One Deployment

Deploy agent-core with all extensions:
```bash
sudo cp agent-core/build/agent-core /usr/local/bin/
sudo cp extensions/*/build/ext-* /usr/local/bin/
```

### Selective Deployment

Deploy only needed extensions:
```bash
# Device with tunnel requirement
sudo cp agent-core/build/agent-core /usr/local/bin/
sudo cp extensions/tunnel/build/ext-tunnel /usr/local/bin/

# Device without tunnel (direct connection)
sudo cp agent-core/build/agent-core /usr/local/bin/
sudo cp extensions/ps-exec/build/ext-ps /usr/local/bin/
```

### Gateway Deployment

Gateways may need different extensions than devices:
```bash
# Gateway-specific extensions
sudo cp agent-core/build/agent-core /usr/local/bin/
sudo cp extensions/tunnel/build/ext-tunnel /usr/local/bin/
# ... additional gateway extensions
```

## Extension Management

Extensions are configured in `agent-core/manifests/extensions.json`:

```json
{
  "extensions": [
    {
      "name": "tunnel",
      "execPath": "/usr/local/bin/ext-tunnel",
      "args": ["--config", "/etc/agent-core/tunnel.json"],
      "critical": true,
      "enabled": true
    },
    {
      "name": "ps-exec",
      "execPath": "/usr/local/bin/ext-ps",
      "args": [],
      "critical": false,
      "enabled": true
    }
  ]
}
```

Agent-core will:
- Launch enabled extensions on startup
- Monitor their health
- Restart crashed extensions
- Quarantine after repeated failures

## Documentation

- **Agent Core**: See `agent-core/README.md` and `agent-core/ARCHITECTURE.md`
- **Tunnel Extension**: See `extensions/tunnel/README.md`
- **PS-Exec Extension**: See `extensions/ps-exec/README.md`
- **Quick Start**: See `agent-core/QUICKSTART.md`

## Testing

### Unit Tests

```bash
cd agent-core
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build
```

### Integration Tests

Extensions can be tested independently before integration:

```bash
# Test extension standalone
cd extensions/tunnel
./build/ext-tunnel --config ./test-config.json
```

## Platform Support

- **Linux**: Ubuntu 20.04+, Debian 11+
- **Windows**: Windows 10/11, Windows Server 2019+

## Dependencies

### Agent Core
- CMake 3.15+
- C++17 compiler
- ZeroMQ (libzmq)
- MQTT C++ Client (Paho)
- OpenSSL
- nlohmann/json

### Extensions
- Minimal dependencies (C++17, ZeroMQ)
- Extension-specific libraries as needed

## Contributing

When adding new extensions:
1. Create independent project under `extensions/`
2. Document message protocol in README.md
3. Follow ZeroMQ envelope contract
4. Add example configurations
5. Include unit tests

