#include "StateMachine.h"
#include "WiFiConnection.h"
#include "WiFiCredentials.h"
#include "IrrigationController.h"
#include <WiFi.h>
#include <MooreArduino.h>

using namespace MooreArduino;

//----------------------------------------------------------------------------//
// Debug Configuration
//----------------------------------------------------------------------------//

// Toggle debug output at compile time
// Set to 1 to enable verbose debug logging, 0 for production builds
// When disabled, all DEBUG_PRINT/DEBUG_PRINTLN calls compile to nothing (zero overhead)
#define DEBUG_ENABLED 1

// Preprocessor macros that conditionally compile debug statements
// These are C++ preprocessor directives - they're replaced at compile time
#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)     // Print without newline
  #define DEBUG_PRINTLN(x) Serial.println(x) // Print with newline
#else
  #define DEBUG_PRINT(x)    // Compiles to nothing
  #define DEBUG_PRINTLN(x)  // Compiles to nothing
#endif

//----------------------------------------------------------------------------//
// External References
//----------------------------------------------------------------------------//

extern MooreMachine<AppState, Input, Output> g_machine;  // Defined in main file

//----------------------------------------------------------------------------//
// Pure State Transition Function δ: Q × Σ → Q
//----------------------------------------------------------------------------//

AppState transitionFunction(const AppState& state, const Input& input) {
  AppState newState = state;          // Copy current state
  newState.lastUpdate = millis();     // Update timestamp on every input
  
  switch (input.type) {
    case INPUT_NONE:
      // No-op input - return state unchanged
      return newState;
      
    case INPUT_REQUEST_CREDENTIALS:
      // User wants to enter new WiFi credentials
      newState.mode = MODE_ENTERING_CREDENTIALS;
      return newState;
      
    case INPUT_CREDENTIALS_ENTERED:
      // User finished entering credentials - prepare for connection
      newState.credentials = input.newCredentials;  // Store new credentials
      newState.credentialsChanged = true;           // Flag for persistence
      newState.shouldReconnect = true;              // Flag for connection attempt
      newState.mode = MODE_CONNECTING;              // Change to connecting state
      return newState;
      
    case INPUT_CONNECTION_STARTED:
      // WiFi.begin() was called - clear the reconnect flag
      newState.shouldReconnect = false;
      return newState;
      
    case INPUT_RETRY_CONNECTION:
      // User requested connection retry
      newState.shouldReconnect = true;   // Set flag for side effects
      newState.mode = MODE_CONNECTING;   // Change to connecting state
      return newState;
      
    case INPUT_WIFI_CONNECTED:
      // Hardware reports successful WiFi connection
      newState.mode = MODE_CONNECTED;           // Update mode
      newState.wifiStatus = input.wifiStatus;  // Store hardware status
      newState.shouldReconnect = false;        // Clear retry flag
      newState.lastPollTime = 0;               // Force immediate HTTP poll
      newState.shouldPollNow = true;           // Flag for immediate polling
      return newState;
      
    case INPUT_WIFI_DISCONNECTED:
      // Hardware reports WiFi connection lost
      newState.mode = MODE_DISCONNECTED;        // Update mode
      newState.wifiStatus = input.wifiStatus;  // Store hardware status
      return newState;
      
    case INPUT_SCHEDULE_RECEIVED:
      // New irrigation schedule received from HTTP endpoint
      newState.schedule = input.newSchedule;
      newState.lastPollTime = millis();
      newState.httpError = false;
      return newState;
      
    case INPUT_HTTP_ERROR:
      // HTTP request failed
      newState.httpError = true;
      newState.lastPollTime = millis();
      return newState;
      
    case INPUT_CREDENTIALS_SAVED:
      // Credentials have been saved to flash - clear the flag
      newState.credentialsChanged = false;
      return newState;
      
    case INPUT_POLL_STARTED:
      // HTTP polling has started - clear the immediate poll flag
      newState.shouldPollNow = false;
      return newState;
      
    case INPUT_TICK: {
      // Connection timeout check (pure logic based on state)
      if (newState.mode == MODE_CONNECTING) {
        unsigned long currentTime = millis();
        if (currentTime - newState.lastUpdate > 30000) { // 30 second timeout
          DEBUG_PRINTLN("DEBUG: Connection timeout, switching to disconnected");
          newState.mode = MODE_DISCONNECTED;
        }
      }
      return newState;
    }
      
    default:
      // Unknown input type - log and return unchanged state
      DEBUG_PRINTLN("Unknown input type in transition function");
      return newState;
  }
}

//----------------------------------------------------------------------------//
// Pure Output Function λ: Q → Γ
//----------------------------------------------------------------------------//

Output outputFunction(const AppState& state) {
  // Priority 1: Handle shouldReconnect flag
  if (state.shouldReconnect) {
    return Output::startWiFiConnection();
  }
  
  // Priority 2: Handle credentials that need saving
  if (state.credentialsChanged) {
    return Output::saveCredentials();
  }
  
  // Priority 3: HTTP polling when connected (immediate or interval based)
  if (state.mode == MODE_CONNECTED) {
    if (state.shouldPollNow) {
      DEBUG_PRINTLN("DEBUG: Immediate HTTP poll triggered");
      return Output::pollSchedule();
    }
    
    unsigned long currentTime = millis();
    unsigned long timeSinceLastPoll = currentTime - state.lastPollTime;
    if (timeSinceLastPoll > 30000) { // 30 second poll interval
      DEBUG_PRINTLN("DEBUG: 30-second HTTP poll triggered");
      return Output::pollSchedule();
    }
  }
  
  // Priority 4: Update zone LEDs based on schedule
  if (state.mode == MODE_CONNECTED && state.schedule.lastUpdate > 0) {
    return Output::updateZones();
  }
  
  // Priority 5: Generate LED effects based on current mode
  switch (state.mode) {
    case MODE_CONNECTED:
      return Output::updateLEDs(state.mode);
      
    case MODE_CONNECTING:
      return Output::updateLEDs(state.mode);
      
    case MODE_DISCONNECTED:
      return Output::updateLEDs(state.mode);
      
    case MODE_ENTERING_CREDENTIALS:
      return Output::updateLEDs(state.mode);
      
    case MODE_INITIALIZING:
      return Output::updateLEDs(state.mode);
  }
  
  return Output::none();
}

//----------------------------------------------------------------------------//
// Output Execution
//----------------------------------------------------------------------------//

Input executeEffect(const Output& effect) {
  switch (effect.type) {
    case EFFECT_UPDATE_LEDS:
      updateLEDs(effect.currentMode);
      break;
      
    case EFFECT_SAVE_CREDENTIALS: {
      const AppState& state = g_machine.getState();
      saveCredentials(&state.credentials);
      // Return input to clear the credentialsChanged flag
      return Input::credentialsSaved();
    }
    
    case EFFECT_START_WIFI_CONNECTION: {
      const AppState& state = g_machine.getState();
      Serial.println("Initiating WiFi connection...");
      connectWiFi(&state.credentials);
      // Return follow-up input to clear shouldReconnect flag
      return Input::connectionStarted();
    }
    
    case EFFECT_RENDER_UI:
      renderUI(effect.currentMode);
      break;
      
    case EFFECT_LOG_CONNECTION_SUCCESS:
      Serial.println("✓ Successfully connected to WiFi!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      break;
      
    case EFFECT_LOG_CONNECTION_LOST:
      Serial.println("✗ WiFi connection lost");
      break;
      
    case EFFECT_POLL_SCHEDULE: {
      // Clear the immediate poll flag first
      Input pollStartedInput = Input::pollStarted();
      g_machine.step(pollStartedInput);
      // Make HTTP request to get irrigation schedule
      return pollIrrigationSchedule();
    }
      
    case EFFECT_UPDATE_ZONES: {
      const AppState& state = g_machine.getState();
      updateZoneLEDs(state.schedule);
      break;
    }
      
    case EFFECT_NONE:
    default:
      // No effect to execute
      break;
  }
  
  return Input::none();
}
