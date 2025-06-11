# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an irrigation control system with two main components:
1. **Arduino Controller** - C++ firmware for an Arduino Giga R1 WiFi board implementing a Moore state machine for WiFi connection management
2. **Haskell Web Server** - Backend API server built with Servant for irrigation scheduling

## Development Commands

### Haskell Web Server
- Build: `just build`
- Run development server: `just run`
- Run tests: `just test`
- Format code: `just format` or `just format-hs`
- Check formatting: `just check-format` or `just check-format-hs`
- Clean build artifacts: `just clean`

### Arduino Controller
- Build Arduino sketch: `just arduino-build`
- Enter development shell: `nix develop`

### Database Operations
- Start dev PostgreSQL: `just postgres-dev-start`
- Stop dev PostgreSQL: `just postgres-dev-stop`
- Connect to dev DB: `just postgres-dev-psql`
- Run migrations: `just migrations-run`
- Add new migration: `just migrations-add MIGRATION_NAME`
- Reset database: `just migrations-reset`

### Observability
- Start Jaeger tracing: `just jaeger-start`
- Stop Jaeger: `just jaeger-stop`

## Architecture

### Arduino Controller Architecture
The Arduino controller implements a Moore finite state machine with these key states:
- `INITIALIZING` - Initial state on startup
- `CONNECTING` - Attempting to connect to WiFi
- `CONNECTED` - Successfully connected to WiFi
- `DISCONNECTED` - Lost WiFi connection
- `ENTERING_CREDENTIALS` - User credential input mode

Key architectural principles:
- Pure functional state transitions with no side effects
- I/O operations handled separately through effect execution
- Persistent WiFi credential storage using KVStore (flash memory)
- Reactive UI updates through state observers

### Haskell Web Server Architecture
Built using `web-server-core` framework with:
- Servant API definitions in `WebServer.hs`
- OpenTelemetry observability integration
- PostgreSQL database with Hasql
- JWT-based authentication system
- Environment-based configuration

## Key Files

### Arduino Controller (`controller/`)
- `controller.ino` - Main Arduino sketch with Moore machine
- `WiFiStateMachine.{h,cpp}` - State machine implementation
- `WiFiConnection.{h,cpp}` - WiFi connection management
- `WiFiCredentials.{h,cpp}` - Credential storage/retrieval
- `WiFiUI.{h,cpp}` - User interface handling

### Web Server (`web-server/`)
- `app/Main.hs` - Application entry point
- `src/WebServer.hs` - API implementation and server logic
- `migrations/` - SQL database migrations

## Development Environment

This project uses Nix flakes for reproducible development environments. The flake provides:
- Haskell development tools (GHC, Cabal, HLS)
- Arduino development tools and libraries
- Database tools (PostgreSQL, sqlx-cli)
- Formatters and linters

## Testing

Run Haskell tests with `just test`. Tests are disabled in the Nix build (`dontCheck`) but can be run in development.

## Deployment

Deploy using `just deploy` which runs `nix run .#deploy`. The system provides a NixOS module for production deployment.