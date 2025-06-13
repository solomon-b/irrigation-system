#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

#include "Types.h"

//----------------------------------------------------------------------------//
// WiFi Credentials Management
//----------------------------------------------------------------------------//

/**
 * Persist WiFi credentials to flash memory using KVStore
 * @param creds Pointer to credentials structure to save
 */
void saveCredentials(const Credentials* creds);

/**
 * Load WiFi credentials from flash memory
 * @param creds Pointer to credentials structure to populate
 * @return true if credentials were found and loaded, false otherwise
 */
bool loadCredentials(Credentials* creds);

/**
 * Prompt user for WiFi credentials via Serial (blocking)
 * @param creds Pointer to credentials structure to populate
 * @return true if valid credentials were entered, false on validation failure
 */
bool promptForCredentialsBlocking(Credentials* creds);

/**
 * Validate WiFi credential length
 * @param credential String to validate (SSID or password)
 * @return true if length is valid (1-63 characters), false otherwise
 */
bool isValidCredentialLength(const String& credential);

/**
 * Clear any pending serial input to prevent stale data
 */
void flushSerialInput();

/**
 * Persist irrigation schedule to flash memory using KVStore
 * @param schedule Pointer to schedule structure to save
 */
void saveSchedule(const IrrigationSchedule* schedule);

/**
 * Load irrigation schedule from flash memory
 * @param schedule Pointer to schedule structure to populate
 * @return true if schedule was found and loaded, false otherwise
 */
bool loadSchedule(IrrigationSchedule* schedule);

#endif // WIFI_CREDENTIALS_H