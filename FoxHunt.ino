/********************************************************************************
 * Morse Code Transmitter for Radio Fox Hunt
 *
 * Original Author: VE6BTC (IDE 1.6.0)
 * Updated: November 2020 by VA6MCP (IDE 1.8.13)
 * 2nd Update: February 2026 by 21M085 (IDE 2.3.7)
 *             Further refinements to the code include improved variable naming, structured comments,
 *             function documentation, and timing adjustments.
 * 3rd Update: Added serial menu configuration.
 * 4th Update: Added timeouts to serial input functions to prevent blocking/hanging when connected to a computer.
 *             This ensures the menu and input reading exit automatically if no complete input is provided within a reasonable time.
 *             Also added small delays in loops to avoid busy-waiting and reduce CPU load.
 * 5th Update: Inverted TX_PIN logic. Now HIGH on TX_PIN starts transmission, LOW stops it.
 *             Updated comments to reflect normally open relay configuration.
 * 6th Update: Updated hardware description to use 4N25 optocoupler instead of relay.
 *
 * Purpose:
 * This sketch enables an Arduino to transmit Morse code via a Baofeng UV-5R or similar radio
 * for amateur radio fox hunting activities. It generates audio tones for the radio's microphone
 * input and controls the PTT (Push-To-Talk) line using a 4N25 optocoupler.
 *
 * Hardware Connections:
 * - Pin 7 (TX_PIN): Controls the PTT via 4N25 optocoupler.
 * - Pin 5 (AUDIO_PIN): Outputs audio tones to the radio's microphone input.
 * - Pin 13: On-board LED for visualizing Morse code output (optional debugging).
 *
 * Configuration:
 * - Transmission starts with a long continuous tone for easy direction finding.
 * - Followed by Morse code identification.
 * - Cycles with a rest period between transmissions.
 * - Serial menu accessible by sending 'x' during rest period for configuration.
 *
 * Notes:
 * - Optocoupler (4N25): HIGH on TX_PIN activates the opto (starts TX), LOW deactivates it (stops TX).
 * - Tone frequency is approximate due to square wave generation and harmonics.
 * - Morse timing follows standard conventions exactly: dit = 1 unit, dah = 3 units, inter-element = 1 unit,
 * inter-character = 3 units, inter-word = 7 units. This is achieved by appending '0' for inter-character
 * (adding 2 units via pause), and using "000" for word spaces (adding three pauses for 6 units extra).
 ********************************************************************************/
#include <EEPROM.h>

#define TX_PIN 7 // Pin controlling PTT via optocoupler.
#define AUDIO_PIN 5 // Pin for audio output to radio mic.
#define EEPROM_MAGIC 0xABCD
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_TONE_HZ 2
#define EEPROM_ADDR_DIT_MS 4
#define EEPROM_ADDR_REST_MS 6
#define EEPROM_ADDR_LONG_TONE_MS 10
#define EEPROM_ADDR_MESSAGE 14 // Max message length assumed 100 chars.
#define MAX_MESSAGE_LEN 100
#define MENU_TIMEOUT_MS 30000 // 30 seconds timeout for menu inactivity.
#define INPUT_TIMEOUT_MS 10000 // 10 seconds timeout for reading input lines.

// Configurable variables (loaded from EEPROM)
int TONE_HZ = 600; // Tone frequency in Hz (actual slightly lower with harmonics).
int DIT_MS = 64; // Dit duration in ms (1 unit); dah and pauses derived from this.
long REST_MS = 60000; // Rest period between transmissions in ms (60 seconds).
long LONG_TONE_MS = 10000; // Long initial tone duration in ms (10 seconds).

// Morse code table: 1 = dit, 2 = dah, 0 = space pause unit.
// Index 0: word space (three pauses for extra 6 units).
// Indices 1-26: A-Z.
// Indices 27-36: 0-9.
const String morseTable[] = {
  "000", // Word space (adjusted for exact 7-unit timing)
  "12", // A
  "2111", // B
  "2121", // C
  "211", // D
  "1", // E
  "1121", // F
  "221", // G
  "1111", // H
  "11", // I
  "1222", // J
  "212", // K
  "1211", // L
  "22", // M
  "21", // N
  "222", // O
  "1221", // P
  "2212", // Q
  "121", // R
  "111", // S
  "2", // T
  "112", // U
  "1112", // V
  "122", // W
  "2112", // X
  "2122", // Y
  "2211", // Z
  "22222", // 0
  "12222", // 1
  "11222", // 2
  "11122", // 3
  "11112", // 4
  "11111", // 5
  "21111", // 6
  "22111", // 7
  "22211", // 8
  "22221" // 9
};

// User-configurable message to transmit in Morse code.
String message = "21M085 FOX 21M085 FOX";
String morseCode;

// Global variables
long notePeriod;

// Function prototypes
String formMorse(const String& input);
void playCode(const String& code);
void playTone(long duration);
void printMenu();
void menu();
void processCommand(char cmd);
String readSerialLine();
void writeStringToEEPROM(int addr, const String& str);
String readStringFromEEPROM(int addr);
void resetToDefaults();

void setup() {
  pinMode(TX_PIN, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT); // Use built-in LED constant for clarity.
  digitalWrite(TX_PIN, LOW); // Initial state: stop transmission (deactivate opto).
  Serial.begin(9600);

  // Load settings from EEPROM
  uint16_t magic;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  if (magic != EEPROM_MAGIC) {
    resetToDefaults();
  } else {
    EEPROM.get(EEPROM_ADDR_TONE_HZ, TONE_HZ);
    EEPROM.get(EEPROM_ADDR_DIT_MS, DIT_MS);
    EEPROM.get(EEPROM_ADDR_REST_MS, REST_MS);
    EEPROM.get(EEPROM_ADDR_LONG_TONE_MS, LONG_TONE_MS);
    message = readStringFromEEPROM(EEPROM_ADDR_MESSAGE);
  }
  morseCode = formMorse(message);
  notePeriod = 1000000L / TONE_HZ;

  // Serial debugging (optional; can be removed in production).
  Serial.print("Message: ");
  Serial.println(message);
  Serial.print("Morse Code: ");
  Serial.println(morseCode);
  Serial.println("Send 'x' during rest period to enter menu.");
}

void loop() {
  digitalWrite(TX_PIN, HIGH); // Start transmission (activate opto).

  // Transmit long tone for direction finding.
  playTone(LONG_TONE_MS);
  delay(250); // Brief pause before Morse code.

  // Transmit Morse code message.
  playCode(morseCode);

  digitalWrite(TX_PIN, LOW); // Stop transmission.

  // Non-blocking rest with serial check
  long startRest = millis();
  while (millis() - startRest < REST_MS) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'x') {
        menu();
        startRest = millis(); // Reset rest timer after menu
      }
    }
    delay(100); // Small delay to avoid busy loop
  }
}

/**
 * Generates a tone or pause for the specified duration.
 * @param duration Tone duration in ms (0 for pause).
 */
void playTone(long duration) {
  if (duration > 0) {
    digitalWrite(LED_BUILTIN, HIGH); // Visualize tone.
    long startTime = millis();
    long elapsed = 0;
    while (elapsed < duration) {
      digitalWrite(AUDIO_PIN, HIGH);
      delayMicroseconds(notePeriod / 2);
      digitalWrite(AUDIO_PIN, LOW);
      delayMicroseconds(notePeriod / 2);
      elapsed = millis() - startTime;
    }
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    // Pause unit: 2 units (to achieve exact inter-character 3 units and inter-word 7 units).
    delay(DIT_MS * 2);
  }
}

/**
 * Plays the Morse code sequence.
 * @param code String of '1' (dit), '2' (dah), '0' (space pause).
 */
void playCode(const String& code) {
  for (size_t i = 0; i < code.length(); ++i) {
    char element = code[i];
    long duration_ms = 0;
    if (element == '1') {
      duration_ms = DIT_MS; // Dit (1 unit).
    } else if (element == '2') {
      duration_ms = DIT_MS * 3; // Dah (3 units).
    } else if (element == '0') {
      duration_ms = 0; // Pause.
    }
    playTone(duration_ms);
    if (element != '0') {
      delay(DIT_MS); // Inter-element pause (1 unit).
    }
  }
}

/**
 * Converts text to Morse code string.
 * @param input Text to convert (uppercase letters, numbers, spaces).
 * @return Morse code representation.
 */
String formMorse(const String& input) {
  String upperInput = input;
  upperInput.toUpperCase();
  String output = "";
  for (size_t i = 0; i < upperInput.length(); ++i) {
    char ch = upperInput[i];
    if (ch >= 'A' && ch <= 'Z') {
      output += morseTable[ch - 'A' + 1] + '0'; // Append single pause for inter-character.
    } else if (ch >= '0' && ch <= '9') {
      output += morseTable[ch - '0' + 27] + '0'; // Append single pause for inter-character.
    } else if (ch == ' ') {
      output += morseTable[0]; // Word space (three pauses).
    }
    // Ignore unsupported characters.
  }
  return output;
}

// Serial Menu Functions
void printMenu() {
  Serial.println("\nMMorse Code Transmitter for Radio Fox Hunt");
  Serial.println("Version 1.0 by 21M085");
  Serial.println("System Configuration");
  Serial.println("Menu:");
  Serial.println("1 - Set message");
  Serial.println("2 - Set tone frequency (Hz)");
  Serial.println("3 - Set dit duration (ms)");
  Serial.println("4 - Set rest period (seconds)");
  Serial.println("5 - Set long tone duration (seconds)");
  Serial.println("6 - Reset to defaults");
  Serial.println("x - Exit menu");
  Serial.print(">");
}

void menu() {
  Serial.println("Entering menu. Transmission paused.");
  printMenu();
  long startMenu = millis();
  while (true) {
    if (Serial.available()) {
      char cmd = Serial.read();
      if (cmd == '\r' || cmd == '\n') continue; // Ignore newlines
      processCommand(cmd);
      if (cmd == 'x') break;
      printMenu();
      startMenu = millis(); // Reset timeout on valid input
    }
    if (millis() - startMenu > MENU_TIMEOUT_MS) {
      Serial.println("\nMenu timeout. Exiting.");
      break;
    }
    delay(10); // Small delay to avoid busy loop
  }
  Serial.println("Resuming transmission.");
}

void processCommand(char cmd) {
  String input;
  switch (cmd) {
    case '1':
      Serial.print("Enter new message: ");
      input = readSerialLine();
      if (input.length() > 0 && input.length() <= MAX_MESSAGE_LEN) {
        message = input;
        morseCode = formMorse(message);
        writeStringToEEPROM(EEPROM_ADDR_MESSAGE, message);
        Serial.println("Message set.");
      } else {
        Serial.println("Invalid message.");
      }
      break;
    case '2':
      Serial.print("Enter new tone frequency (Hz): ");
      input = readSerialLine();
      int newTone = input.toInt();
      if (newTone > 0) {
        TONE_HZ = newTone;
        notePeriod = 1000000L / TONE_HZ;
        EEPROM.put(EEPROM_ADDR_TONE_HZ, TONE_HZ);
        Serial.println("Tone frequency set.");
      } else {
        Serial.println("Invalid value.");
      }
      break;
    case '3':
      Serial.print("Enter new dit duration (ms): ");
      input = readSerialLine();
      int newDit = input.toInt();
      if (newDit > 0) {
        DIT_MS = newDit;
        EEPROM.put(EEPROM_ADDR_DIT_MS, DIT_MS);
        Serial.println("Dit duration set.");
      } else {
        Serial.println("Invalid value.");
      }
      break;
    case '4':
      Serial.print("Enter new rest period (seconds): ");
      input = readSerialLine();
      long newRest = input.toInt() * 1000L;
      if (newRest > 0) {
        REST_MS = newRest;
        EEPROM.put(EEPROM_ADDR_REST_MS, REST_MS);
        Serial.println("Rest period set.");
      } else {
        Serial.println("Invalid value.");
      }
      break;
    case '5':
      Serial.print("Enter new long tone duration (seconds): ");
      input = readSerialLine();
      long newLong = input.toInt() * 1000L;
      if (newLong > 0) {
        LONG_TONE_MS = newLong;
        EEPROM.put(EEPROM_ADDR_LONG_TONE_MS, LONG_TONE_MS);
        Serial.println("Long tone duration set.");
      } else {
        Serial.println("Invalid value.");
      }
      break;
    case '6':
      resetToDefaults();
      Serial.println("Reset to defaults complete.");
      break;
    case 'x':
      Serial.println("Exiting menu.");
      break;
    default:
      Serial.println("Unknown command.");
      break;
  }
}

String readSerialLine() {
  String line = "";
  long start = millis();
  while (millis() - start < INPUT_TIMEOUT_MS) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n') {
        return line;
      }
      line += c;
      start = millis(); // Reset timeout on each character received
    }
    delay(10); // Small delay to avoid busy loop
  }
  Serial.println("\nInput timeout.");
  return "";
}

void writeStringToEEPROM(int addr, const String& str) {
  byte len = str.length();
  EEPROM.write(addr, len);
  for (byte i = 0; i < len; i++) {
    EEPROM.write(addr + 1 + i, str[i]);
  }
  EEPROM.write(addr + 1 + len, 0); // Null terminator
}

String readStringFromEEPROM(int addr) {
  String str = "";
  byte len = EEPROM.read(addr);
  if (len > MAX_MESSAGE_LEN) len = MAX_MESSAGE_LEN; // Safety
  for (byte i = 0; i < len; i++) {
    str += (char)EEPROM.read(addr + 1 + i);
  }
  return str;
}

void resetToDefaults() {
  TONE_HZ = 600;
  DIT_MS = 64;
  REST_MS = 60000;
  LONG_TONE_MS = 10000;
  message = "21M085 FOX 21M085 FOX";
  morseCode = formMorse(message);
  notePeriod = 1000000L / TONE_HZ;
  EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR_TONE_HZ, TONE_HZ);
  EEPROM.put(EEPROM_ADDR_DIT_MS, DIT_MS);
  EEPROM.put(EEPROM_ADDR_REST_MS, REST_MS);
  EEPROM.put(EEPROM_ADDR_LONG_TONE_MS, LONG_TONE_MS);
  writeStringToEEPROM(EEPROM_ADDR_MESSAGE, message);
}
