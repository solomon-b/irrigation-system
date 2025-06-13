/*
 * Irrigation Controller - WiFi Connection Manager & Zone Control
 * 
 * Manages WiFi connectivity and controls irrigation zones based on schedule data
 * received from a web server using a state machine approach for reliable operation.
 * 
 * Business Domain:
 * This controller maintains WiFi connectivity and controls irrigation zones by:
 * - Receiving scheduling commands from the web server (configurable hostname/port)
 * - Controlling 3 irrigation zones via LED indicators
 * - Providing real-time status feedback through LEDs
 * - Allowing local WiFi credential management
 * 
 * WiFi Connection States:
 * - INITIALIZING: System startup, checking for saved credentials
 * - CONNECTING: Attempting to connect with current credentials
 * - CONNECTED: Successfully connected, polling irrigation schedule
 * - DISCONNECTED: Connection lost, attempting reconnection
 * - ENTERING_CREDENTIALS: User is inputting new WiFi credentials
 * 
 * Hardware:
 * - Arduino Giga R1 WiFi board
 * - Power LED (Pin 2): Always on when powered
 * - WiFi LED (Pin 3): Shows connection status (solid=connected, blink=connecting)
 * - Zone 1 LED (Pin 4): Irrigation zone 1 status
 * - Zone 2 LED (Pin 5): Irrigation zone 2 status  
 * - Zone 3 LED (Pin 6): Irrigation zone 3 status
 * - Serial interface (115200 baud): User interaction and debugging
 * 
 * Irrigation Schedule:
 * - Polls configured server every 30 seconds when connected
 * - Expects JSON: {"zone1":true,"zone2":false,"zone3":true}
 * - LEDs reflect current zone activation state
 * - Schedule data cached with 5-minute staleness detection
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
// HTTP client for polling irrigation schedule
#include <ArduinoHttpClient.h>
// JSON parsing for schedule data
#include <ArduinoJson.h>
// Key-Value store API for persistent credential storage in flash memory
#include "kvstore_global_api.h"
// Mbed error handling definitions
#include <mbed_error.h>
// MooreArduino library for Moore machine implementation and utilities
#include <MooreArduino.h>

// Local modules
#include "Types.h"
#include "WiFiCredentials.h"
#include "WiFiConnection.h"
#include "IrrigationController.h"
#include "StateMachine.h"

using namespace MooreArduino;

//----------------------------------------------------------------------------//
// Debug Configuration
// ----------------------------------------------------------------------------//
// TODO: Can we consolidate all these debuging definitions across modules into
// one place and maybe use an argv flag?

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
const int zone1_led_pin = 4;  // Zone 1 irrigation LED
const int zone2_led_pin = 5;  // Zone 2 irrigation LED
const int zone3_led_pin = 6;  // Zone 3 irrigation LED

//----------------------------------------------------------------------------//
// Network Configuration
//----------------------------------------------------------------------------//

// HTTP server configuration for irrigation schedule polling
const char* server_hostname = "192.168.5.7";  // Server hostname or IP address  
const int server_port = 3000;           // Server port number

//----------------------------------------------------------------------------//
// Global State Management
//----------------------------------------------------------------------------//

// Moore machine instance with transition function and initial state
MooreMachine<AppState, Input, Output> g_machine(transitionFunction, AppState());

// Global utilities
Timer g_tickTimer(100);         // 100ms tick rate (10Hz)
Button g_resetButton(13);       // Reset button on pin 13

// HTTP client for irrigation schedule polling
WiFiClient g_wifiClient;
HttpClient g_httpClient(g_wifiClient, server_hostname, server_port);

//----------------------------------------------------------------------------//
// Arduino Setup Function
//----------------------------------------------------------------------------//

void setup() {
  // Configure LED pins as outputs
  pinMode(wifi_led_pin, OUTPUT);    // WiFi status LED
  pinMode(power_led_pin, OUTPUT);   // Power indicator LED
  pinMode(zone1_led_pin, OUTPUT);   // Zone 1 LED
  pinMode(zone2_led_pin, OUTPUT);   // Zone 2 LED
  pinMode(zone3_led_pin, OUTPUT);   // Zone 3 LED
  
  // Initialize LEDs
  digitalWrite(power_led_pin, HIGH); // Turn on power LED immediately
  digitalWrite(wifi_led_pin, LOW);   // WiFi LED starts off
  digitalWrite(zone1_led_pin, LOW);  // Zone LEDs start off
  digitalWrite(zone2_led_pin, LOW);
  digitalWrite(zone3_led_pin, LOW);

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
  // TODO: This should be provided when construction g_machine.
  g_machine.setOutputFunction(outputFunction);
  
  // Start timer
  g_tickTimer.start();
  
  // Display initial state for debugging
  Serial.println("=== Irrigation Controller Starting ===");
  Serial.print("Initial state mode: ");
  Serial.println(g_machine.getState().mode);
  
  // Attempt to load saved WiFi credentials from flash memory
  Credentials loadedCreds;
  bool hasCredentials = false;
  if (!loadCredentials(&loadedCreds)) {
    Serial.println("No stored credentials found.");
    // No credentials found - start credential entry process
    Serial.println("Requesting credentials...");
    g_machine.step(Input::requestCredentials());
  } else {
    Serial.print("Loaded credentials for SSID: ");
    Serial.println(loadedCreds.ssid);
    // Credentials found - inject them into state and attempt to connect
    g_machine.step(Input::credentialsEntered(loadedCreds));
    hasCredentials = true;
  }
  
  // Attempt to load saved irrigation schedule from flash memory
  IrrigationSchedule loadedSchedule;
  bool hasSchedule = false;
  if (loadSchedule(&loadedSchedule)) {
    // Schedule found and valid - update timestamp and inject it into state
    loadedSchedule.lastUpdate = millis();  // Update to current boot time
    g_machine.step(Input::scheduleReceived(loadedSchedule));
    hasSchedule = true;
    
    Serial.print("Loaded schedule: zones=");
    Serial.print(loadedSchedule.zone1 ? "1" : "0");
    Serial.print(loadedSchedule.zone2 ? "1" : "0");
    Serial.println(loadedSchedule.zone3 ? "1" : "0");
    
    // Force immediate zone LED update during setup
    updateZoneLEDs(loadedSchedule);
  }
  
  // If we have credentials but no schedule, trigger immediate poll when connected
  if (hasCredentials && !hasSchedule) {
    Serial.println("No saved schedule - will poll immediately when connected");
    // The shouldPollNow flag will be set when WiFi connects
  }
  
  Serial.println("=== Setup Complete ===");
}

//----------------------------------------------------------------------------//
// Arduino Main Loop
//----------------------------------------------------------------------------//

void loop() {
  const AppState& state = g_machine.getState();
  
  
  // Status summary every 10 seconds  
  static unsigned long lastStatusOutput = 0;
  if (millis() - lastStatusOutput > 10000) {
    DEBUG_PRINT("Status: mode=");
    DEBUG_PRINT(state.mode);
    DEBUG_PRINT(", zones=");
    DEBUG_PRINT(state.schedule.zone1 ? "1" : "0");
    DEBUG_PRINT(state.schedule.zone2 ? "1" : "0");
    DEBUG_PRINTLN(state.schedule.zone3 ? "1" : "0");
    lastStatusOutput = millis();
  }
  
  // 1. Read events from environment (user input, hardware status)
  Input input = readEvents();
  
  // 2. Check for polling timer expiry when connected (let outputFunction handle the timing)
  if (input.type == INPUT_NONE && g_tickTimer.expired()) {
    input = Input::tick(); // Regular tick for timeout checks and polling
    g_tickTimer.restart();
  }
  
  if (input.type != INPUT_NONE) {
    // Don't flood serial with tick inputs (type 9), only show interesting events
    if (input.type != INPUT_TICK) {
      DEBUG_PRINT("DEBUG: Input type=");
      DEBUG_PRINTLN(input.type);
    }
    
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
      if (followUpInput.type != INPUT_TICK) {
        DEBUG_PRINT("DEBUG: Follow-up input type=");
        DEBUG_PRINTLN(followUpInput.type);
      }
      g_machine.step(followUpInput);
    }
  }
  
  // Always generate and execute output based on current state (Moore machine behavior)
  Output currentOutput = outputFunction(state);
  if (currentOutput.type != EFFECT_NONE) {
    Input outputInput = executeEffect(currentOutput);
    if (outputInput.type != INPUT_NONE) {
      if (outputInput.type != INPUT_TICK) {
        DEBUG_PRINT("DEBUG: Output generated input type=");
        DEBUG_PRINTLN(outputInput.type);
      }
      g_machine.step(outputInput);
    }
  }
  
  // Always update LEDs (needed for blinking and responsive indicators)
  updateLEDs(state.mode);
  
  // Always update zone LEDs when we have a valid schedule
  if (state.schedule.lastUpdate > 0) {
    updateZoneLEDs(state.schedule);
  }
  
  delay(10);  // Small delay to prevent overwhelming the system
}
