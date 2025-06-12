#include "IrrigationController.h"
#include "WiFiCredentials.h"
#include <WiFi.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

// External HTTP client and server config from main file
extern HttpClient g_httpClient;
extern const char* server_hostname;
extern const int server_port;


//----------------------------------------------------------------------------//
// LED Control Functions
//----------------------------------------------------------------------------//

void updateLEDs(AppMode mode) {
  switch (mode) {
    case MODE_CONNECTED:
      // Solid on when connected
      digitalWrite(wifi_led_pin, HIGH);
      break;
    case MODE_CONNECTING:
      // Blink at 2Hz during connection attempt
      digitalWrite(wifi_led_pin, (millis() / 250) % 2);  // Toggle every 250ms
      break;
    default:
      // Off for all other modes (disconnected, initializing, entering credentials)
      digitalWrite(wifi_led_pin, LOW);
      break;
  }
}

void updateZoneLEDs(const IrrigationSchedule& schedule) {
  // Update each zone LED based on the schedule
  digitalWrite(zone1_led_pin, schedule.zone1 ? HIGH : LOW);
  digitalWrite(zone2_led_pin, schedule.zone2 ? HIGH : LOW);
  digitalWrite(zone3_led_pin, schedule.zone3 ? HIGH : LOW);
}

//----------------------------------------------------------------------------//
// Serial UI Functions
//----------------------------------------------------------------------------//

void renderUI(AppMode mode) {
  switch (mode) {
    case MODE_CONNECTED:
      // Show network details and available commands
      printCurrentNet();  // Display SSID, IP, signal strength, etc.
      Serial.println("Send 'c' to change credentials.");
      break;
    case MODE_DISCONNECTED:
      // Show retry and credential change options
      Serial.println("Not connected. Send 'r' to retry or 'c' to change credentials.");
      break;
    case MODE_CONNECTING:
      // Simple status message during connection attempt
      Serial.println("Connecting...");
      break;
    case MODE_ENTERING_CREDENTIALS:
      // No message here - credential entry function handles its own prompts
      break;
    case MODE_INITIALIZING:
      // Startup message
      Serial.println("Initializing...");
      break;
  }
}

void printCurrentNet() {
  // Display network name
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // Display router's MAC address (BSSID = Basic Service Set Identifier)
  byte bssid[6];
  WiFi.BSSID(bssid);  // Get 6-byte MAC address
  Serial.print("BSSID: ");
  printMacAddress(bssid);  // Format and print MAC address

  // Display signal strength in dBm (decibels relative to milliwatt)
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.println(rssi);

  // Display security protocol (WEP, WPA, WPA2, etc.)
  byte encryption = WiFi.encryptionType();
  Serial.print("Encryption Type:");
  Serial.println(encryption, HEX);  // Print as hexadecimal
  
  // Show available user commands
  Serial.println("Send 'c' to change credentials.");
  Serial.println();  // Blank line for readability
}

void printMacAddress(byte mac[]) {
  // Loop through 6 bytes in reverse order (network byte order)
  for (int i = 5; i >= 0; i--) {
    // Add leading zero for single-digit hex values
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);  // Print byte as hexadecimal
    // Add colon separator except after last byte
    if (i > 0) {
      Serial.print(":");
    }
  }
  Serial.println();  // End with newline
}

char readSingleChar() {
  if (!Serial.available()) return '\0';  // No input available
  char input = Serial.read();            // Read first byte
  flushSerialInput();                    // Discard remaining input
  return input;                          // Return the character
}

//----------------------------------------------------------------------------//
// State Observers (Reactive UI Updates)
//----------------------------------------------------------------------------//

void observeConnectedState(const AppState& oldState, const AppState& newState) {
  // Only trigger when transitioning TO connected state
  if (oldState.mode != MODE_CONNECTED && newState.mode == MODE_CONNECTED) {
    Serial.println("✓ Successfully connected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void observeDisconnectedState(const AppState& oldState, const AppState& newState) {
  // Only trigger when transitioning FROM connected TO disconnected
  if (oldState.mode == MODE_CONNECTED && newState.mode == MODE_DISCONNECTED) {
    Serial.println("✗ WiFi connection lost");
  }
}

void observeCredentialChanges(const AppState& oldState, const AppState& newState) {
  // Trigger when credentialsChanged flag is set (before persistence)
  if (!oldState.credentialsChanged && newState.credentialsChanged) {
    Serial.println("💾 Credentials will be saved");
  }
}

//----------------------------------------------------------------------------//
// Debug Helper Functions
//----------------------------------------------------------------------------//

#if DEBUG_ENABLED
const char* getModeString(AppMode mode) {
  switch (mode) {
    case MODE_INITIALIZING: return "INITIALIZING";            // Startup phase
    case MODE_CONNECTING: return "CONNECTING";                // WiFi connection in progress
    case MODE_CONNECTED: return "CONNECTED";                  // Successfully connected
    case MODE_DISCONNECTED: return "DISCONNECTED";            // Not connected to WiFi
    case MODE_ENTERING_CREDENTIALS: return "ENTERING_CREDENTIALS"; // User typing credentials
    default: return "UNKNOWN";                               // Invalid mode (shouldn't happen)
  }
}
#endif

//----------------------------------------------------------------------------//
// HTTP Communication Functions
//----------------------------------------------------------------------------//

Input pollIrrigationSchedule() {
  // Only poll if WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot poll: WiFi not connected");
    return Input::httpError();
  }

  Serial.print("Polling irrigation schedule from ");
  Serial.print(server_hostname);
  Serial.print(":");
  Serial.println(server_port);
  
  // Make HTTP GET request
  g_httpClient.get("/");
  
  // Wait for response
  int statusCode = g_httpClient.responseStatusCode();
  String response = g_httpClient.responseBody();
  
  Serial.print("HTTP Status: ");
  Serial.print(statusCode);
  Serial.print(", Response: ");
  Serial.println(response);
  
  if (statusCode != 200) {
    Serial.println("HTTP request failed");
    return Input::httpError();
  }
  
  // Parse JSON response
  IrrigationSchedule schedule;
  if (!parseScheduleJson(response, &schedule)) {
    Serial.println("Failed to parse JSON response");
    return Input::httpError();
  }
  
  Serial.println("Schedule received successfully");
  return Input::scheduleReceived(schedule);
}

bool parseScheduleJson(const String& json, IrrigationSchedule* schedule) {
  // Create JSON document for parsing
  JsonDocument doc;
  
  // Parse JSON
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return false;
  }
  
  // Extract zone states
  schedule->zone1 = doc["zone1"] | false;  // Default to false if key missing
  schedule->zone2 = doc["zone2"] | false;
  schedule->zone3 = doc["zone3"] | false;
  schedule->lastUpdate = millis();
  
  Serial.print("Zone schedule: ");
  Serial.print(schedule->zone1 ? "1" : "0");
  Serial.print(schedule->zone2 ? "1" : "0");
  Serial.println(schedule->zone3 ? "1" : "0");
  
  return true;
}