/*
 * Irrigation Controller - WiFi Connection Manager
 * 
 * Manages WiFi connectivity for an irrigation control system using a state machine
 * approach for reliable connection handling and user credential management.
 * 
 * Business Domain:
 * This controller maintains WiFi connectivity so the irrigation system can:
 * - Receive scheduling commands from the web server
 * - Report system status and sensor data
 * - Allow remote monitoring and control
 * 
 * WiFi Connection States:
 * - INITIALIZING: System startup, checking for saved credentials
 * - CONNECTING: Attempting to connect with current credentials
 * - CONNECTED: Successfully connected, ready for irrigation operations
 * - DISCONNECTED: Connection lost, attempting reconnection
 * - ENTERING_CREDENTIALS: User is inputting new WiFi credentials
 * 
 * Hardware:
 * - Arduino Giga R1 WiFi board
 * - Power LED (Pin 2): Always on when powered
 * - WiFi LED (Pin 3): Shows connection status (solid=connected, blink=connecting)
 * - Serial interface (115200 baud): User interaction and debugging
 * 
 * User Commands:
 * - 'c': Change WiFi credentials
 * - 'r': Retry connection when disconnected
 * 
 * Persistent Storage:
 * - WiFi credentials saved to flash memory (survives power cycles)
 */

// Arduino WiFi library for managing wireless connections
#include <WiFi.h>
// Key-Value store API for persistent credential storage in flash memory
#include "kvstore_global_api.h"
// Mbed error handling definitions
#include <mbed_error.h>
// MooreArduino library for Moore machine implementation and utilities
#include <MooreArduino.h>

// Local modules
#include "WiFiTypes.h"
#include "WiFiCredentials.h"
#include "WiFiConnection.h"
#include "WiFiUI.h"
#include "WiFiStateMachine.h"

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
// Hardware Configuration
//----------------------------------------------------------------------------//

// Arduino pin assignments for LED indicators
// Digital pins can be HIGH (3.3V) or LOW (0V)
const int power_led_pin = 2;  // Power indicator LED (always on when board is powered)
const int wifi_led_pin = 3;   // WiFi status LED (on when connected, blinks when connecting)

//----------------------------------------------------------------------------//
// Global State Management
//----------------------------------------------------------------------------//

// Moore machine instance with transition function and initial state
MooreMachine<AppState, Input, Output> g_machine(transitionFunction, AppState());

// Global utilities
Timer g_tickTimer(100);  // 100ms tick rate (10Hz)
Button g_resetButton(4); // Optional reset button on pin 4

//----------------------------------------------------------------------------//
// Arduino Setup Function
//----------------------------------------------------------------------------//

void setup() {
  // Configure LED pins as outputs
  pinMode(wifi_led_pin, OUTPUT);    // WiFi status LED
  pinMode(power_led_pin, OUTPUT);   // Power indicator LED
  digitalWrite(power_led_pin, HIGH); // Turn on power LED immediately
  digitalWrite(wifi_led_pin, LOW);   // WiFi LED starts off

  // Initialize serial communication at 115200 baud
  Serial.begin(115200);
  while (!Serial);  // Wait for serial port to connect (USB serial)

  delay(1000);  // Brief pause for system stabilization

  // Verify WiFi hardware is present
  Serial.println("Checking WiFi module...");
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("ERROR: WiFi module not detected!");
    // Infinite error loop with fast blinking WiFi LED
    while (true) {
      digitalWrite(wifi_led_pin, HIGH);
      delay(100);
      digitalWrite(wifi_led_pin, LOW);
      delay(100);
    }
  }
  
  // Log current WiFi module status for debugging
  Serial.print("WiFi module status: ");
  Serial.println(WiFi.status());
  
  // Display firmware version for diagnostics
  Serial.print("WiFi firmware: ");
  Serial.println(WiFi.firmwareVersion());

  // Set up state observers for reactive UI updates
  g_machine.addStateObserver(observeConnectedState);
  g_machine.addStateObserver(observeDisconnectedState);
  g_machine.addStateObserver(observeCredentialChanges);
  
  // Set up output function for side effects
  g_machine.setOutputFunction(outputFunction);
  
  // Start tick timer
  g_tickTimer.start();
  
  // Attempt to load saved WiFi credentials from flash memory
  Credentials loadedCreds;
  if (!loadCredentials(&loadedCreds)) {
    Serial.println("No stored credentials.");
    // No credentials found - start credential entry process
    g_machine.step(Input::requestCredentials());
  } else {
    // Credentials found - inject them into state and attempt to connect
    g_machine.step(Input::credentialsEntered(loadedCreds));
  }
}

//----------------------------------------------------------------------------//
// Arduino Main Loop
//----------------------------------------------------------------------------//

void loop() {
  const AppState& state = g_machine.getState();
  
  // 1. Read events from environment (user input, hardware status)
  Input input = readEvents();
  
  if (input.type != INPUT_NONE) {
    DEBUG_PRINT("DEBUG: Input type=");
    DEBUG_PRINTLN(input.type);
    
    // Handle credential entry (blocking operation for better UX)
    if (input.type == INPUT_REQUEST_CREDENTIALS) {
      g_machine.step(input);  // Enter credential entry mode
      
      // Prompt user for credentials (blocking)
      Credentials newCreds;
      if (promptForCredentialsBlocking(&newCreds)) {
        DEBUG_PRINTLN("DEBUG: Processing entered credentials");
        g_machine.step(Input::credentialsEntered(newCreds));
      } else {
        // Credential entry cancelled - return to WiFi status-based state
        g_machine.step(Input::tick());
      }
    } else {
      // Process input through state machine
      g_machine.step(input);
    }
    
    // Execute any side effects from state change
    Output effect = g_machine.getCurrentOutput();
    Input followUpInput = executeEffect(effect);
    
    // Handle any follow-up inputs from effect execution
    if (followUpInput.type != INPUT_NONE) {
      DEBUG_PRINT("DEBUG: Follow-up input type=");
      DEBUG_PRINTLN(followUpInput.type);
      g_machine.step(followUpInput);
    }
  }
  
  // Always update LEDs (needed for blinking and responsive indicators)
  updateLEDs(state.mode);
  
  delay(10);  // Small delay to prevent overwhelming the system
}
