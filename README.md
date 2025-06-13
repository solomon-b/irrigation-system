# Irrigation Control System

This is an irrigation control system with two main components:
1. **Arduino Controller** - C++ firmware for an Arduino Giga R1 WiFi board implementing a Moore state machine for controlling solenoid valves.
2. **Haskell Web Server** - Backend API server built with Servant for irrigation scheduling.

## Architecture

### Arduino Controller State Machine

The Arduino controller implements a Moore finite state machine for WiFi connection management and irrigation scheduling. The state machine handles WiFi connectivity, user credential input, and HTTP polling for irrigation schedules.

```mermaid
stateDiagram-v2
    [*] --> INITIALIZING
    
    INITIALIZING --> ENTERING_CREDENTIALS : INPUT_REQUEST_CREDENTIALS<br/>('c' command)
    INITIALIZING --> CONNECTING : INPUT_CREDENTIALS_ENTERED<br/>(credentials loaded)
    
    ENTERING_CREDENTIALS --> CONNECTING : INPUT_CREDENTIALS_ENTERED<br/>(user input complete)
    
    CONNECTING --> CONNECTED : INPUT_WIFI_CONNECTED<br/>(WiFi.status() success)
    CONNECTING --> DISCONNECTED : INPUT_WIFI_DISCONNECTED<br/>OR INPUT_TICK (30s timeout)
    CONNECTING --> ENTERING_CREDENTIALS : INPUT_REQUEST_CREDENTIALS<br/>('c' command)
    
    CONNECTED --> DISCONNECTED : INPUT_WIFI_DISCONNECTED<br/>(connection lost)
    CONNECTED --> ENTERING_CREDENTIALS : INPUT_REQUEST_CREDENTIALS<br/>('c' command)
    CONNECTED --> CONNECTED : INPUT_SCHEDULE_RECEIVED<br/>INPUT_HTTP_ERROR
    
    DISCONNECTED --> CONNECTING : INPUT_RETRY_CONNECTION<br/>('r' command)
    DISCONNECTED --> CONNECTED : INPUT_WIFI_CONNECTED<br/>(automatic reconnect)
    DISCONNECTED --> ENTERING_CREDENTIALS : INPUT_REQUEST_CREDENTIALS<br/>('c' command)
    
    note right of INITIALIZING
        Effects:
        - Power LED on
        - Load credentials from flash
    end note
    
    note right of ENTERING_CREDENTIALS
        Effects:
        - Serial UI for credential input
        - WiFi LED off
        - Render UI prompts
    end note
    
    note right of CONNECTING
        Effects:
        - WiFi LED blinking
        - Start WiFi connection
        - 30-second timeout
    end note
    
    note right of CONNECTED
        Effects:
        - WiFi LED solid
        - HTTP polling every 30s
        - Update irrigation zones
    end note
    
    note right of DISCONNECTED
        Effects:
        - WiFi LED off
        - Log connection lost
        - Wait for user action
    end note
```

#### Key Input Events

- `INPUT_REQUEST_CREDENTIALS` - User pressed 'c' to enter new credentials
- `INPUT_RETRY_CONNECTION` - User pressed 'r' to retry connection
- `INPUT_WIFI_CONNECTED` - Hardware detected successful WiFi connection
- `INPUT_WIFI_DISCONNECTED` - Hardware detected WiFi connection loss
- `INPUT_TICK` - Timer event for timeout checks and periodic operations
- `INPUT_SCHEDULE_RECEIVED` - HTTP response with irrigation schedule received
- `INPUT_HTTP_ERROR` - HTTP request failed
- `INPUT_CREDENTIALS_ENTERED` - User completed credential entry

## Development

### Quick Start

```bash
# Build Haskell web server
just build

# Build Arduino controller
just arduino-build

# Start development environment
nix develop
```

## Components

### Arduino Controller (`controller/`)
- `controller.ino` - Main Arduino sketch with Moore state machine
- `StateMachine.{h,cpp}` - Pure functional state machine implementation
- `WiFiConnection.{h,cpp}` - WiFi connection management
- `WiFiCredentials.{h,cpp}` - Credential storage/retrieval from flash
- `IrrigationController.{h,cpp}` - Main controller logic
- `Types.h` - State machine type definitions

### Web Server (`web-server/`)
- `app/Main.hs` - Application entry point
- `src/WebServer.hs` - Servant API implementation
- `migrations/` - SQL database migrations

## License

See [LICENSE](LICENSE) for details.
