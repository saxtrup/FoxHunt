/********************************************************************************
 * Morse Code Transmitter for Radio Fox Hunt
 *
 * Based on original work by VE6BTC (IDE 1.6.0),
 * with subsequent updates by VA6MCP in November 2020 (IDE 1.8.13).
 *
 * Major rewrite and functional expansion by 21M085 (IDE 2.3.7), February 2026.
 *
 * Feb 21–23, 2026:
 * - Comprehensive code cleanup + non-blocking design
 * - Robust serial menu
 * - Software DTMF activation on A0 (PhoneDTMF library)
 * - NEW: Menu option 8 to toggle Periodic Beacon ON/OFF
 * • ON (default): automatic transmissions every REST_MS (like original)
 * • OFF: completely silent after power-up until first DTMF code received
 * (perfect for hidden foxes)
 * • Setting saved in EEPROM
 * - DTMF always forces an immediate transmission (even in silent mode)
 *
 * March 2026 Update:
 * - Integrated melody option as alternative to long tone
 * - New menu option 9 to toggle between long tone and melody for intro sound
 * - Melody based on Liquido Narcotic jingle
 * - Added handling for lowercase letters in message without converting entire string to uppercase
 *
 * Purpose:
 * Arduino Micro + Baofeng UV-5R fox for amateur radio fox hunting.
 *
 * Hardware:
 * - Pin 7 → PTT via 4N25 optocoupler (HIGH = TX)
 * - Pin 5 → Audio tone to mic (100 µF cap in series + 330ohm)
 * - Pin A0 → Radio speaker audio (4.7–10 µF cap in series + 10k/10k bias to 2.5 V)
 * - Pin 13 → LED (visual feedback)
 *
 * Notes:
 * - Install PhoneDTMF library from https://github.com/Adrianotiger/phoneDTMF (ZIP)
 * - Default DTMF code = 1985 (or 1985#)
 * - Default periodic = ON
 * - Default intro sound = Long tone
 ********************************************************************************/
#include <EEPROM.h>
#include <PhoneDTMF.h>
// ====================== PIN DEFINES ======================
#define TX_PIN 7
#define AUDIO_PIN 5
// ====================== EEPROM ======================
#define EEPROM_MAGIC 0xABCD
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_ADDR_TONE_HZ 2
#define EEPROM_ADDR_DIT_MS 4
#define EEPROM_ADDR_REST_MS 6
#define EEPROM_ADDR_LONG_TONE_MS 10
#define EEPROM_ADDR_MESSAGE 14
#define EEPROM_ADDR_DTMF_CODE 120
#define EEPROM_ADDR_PERIODIC 130 // NEW: periodic mode flag
#define EEPROM_ADDR_USE_MELODY 131 // NEW: use melody flag
#define MAX_MESSAGE_LEN 100
// ====================== TIMEOUTS ======================
#define MENU_TIMEOUT_MS 30000
#define INPUT_TIMEOUT_MS 10000
#define MESSAGE_TIMEOUT_MS 60000
// Configurable variables
int TONE_HZ = 600;
int DIT_MS = 64;
long REST_MS = 180000; // 3 minutes
long LONG_TONE_MS = 10000; // 10 seconds
// Melody definitions
const int EB4 = 311;
const int F4 = 349;
const int GB4 = 370;
const int AB4 = 415;
const int BB4 = 466;
const int NOTE_DURATION = 220;
const int melody[] = {
  F4, EB4, F4, EB4, F4, EB4, F4, EB4,
  GB4, F4, GB4, F4, GB4, F4, GB4, F4,
  BB4, AB4, BB4, AB4, BB4, AB4, BB4, AB4,
  F4, EB4, F4, EB4, F4, GB4
};
const int numNotes = sizeof(melody) / sizeof(melody[0]);
// NEW: Use melody instead of long tone
bool useMelody = false;
// DTMF
String dtmfActivationCode = "1985";
String dtmfBuffer = "";
unsigned long dtmfLastDigitTime = 0;
const unsigned long DTMF_INTER_DIGIT_TIMEOUT = 2500;
bool forceImmediateTX = false;
PhoneDTMF dtmfDecoder;
// Periodic mode (NEW)
bool periodicEnabled = true;
// Morse table
const String morseTable[] = {
"000", "12","2111","2121","211","1","1121","221","1111","11",
"1222","212","1211","22","21","222","1221","2212","121",
"111","2","112","1112","122","2112","2122","2211",
"22222","12222","11222","11122","11112","11111",
"21111","22111","22211","22221"
};
String message = "21M085 FOX 21M085 FOX";
String morseCode;
// ====================== PROTOTYPES ======================
String formMorse(const String& input);
void playCode(const String& code);
void playNote(int freq, long duration);
void playMelody();
long calculateMorseDuration(const String& code);
long calculateIntroDuration();
void printMenu();
void menu();
void processCommand(char cmd);
String readSerialLine(long timeoutMs = INPUT_TIMEOUT_MS, bool printTimeoutMsg = true);
void writeStringToEEPROM(int addr, const String& str);
String readStringFromEEPROM(int addr);
void resetToDefaults();
void checkSoftwareDTMF();
void processDTMFCommand(String cmd);
void performTransmission(); // NEW helper
void setup() {
pinMode(TX_PIN, OUTPUT);
pinMode(AUDIO_PIN, OUTPUT);
pinMode(LED_BUILTIN, OUTPUT);
digitalWrite(TX_PIN, LOW);
Serial.begin(9600);
  // Load EEPROM
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
    dtmfActivationCode = readStringFromEEPROM(EEPROM_ADDR_DTMF_CODE);
if (dtmfActivationCode.length() == 0) dtmfActivationCode = "1985";
    byte p;
EEPROM.get(EEPROM_ADDR_PERIODIC, p);
    periodicEnabled = (p != 0);
    byte u;
EEPROM.get(EEPROM_ADDR_USE_MELODY, u);
    useMelody = (u != 0);
  }
  morseCode = formMorse(message);
dtmfDecoder.begin(A0);
Serial.print("Message: "); Serial.println(message);
Serial.print("Morse: "); Serial.println(morseCode);
Serial.print("DTMF code: "); Serial.println(dtmfActivationCode);
Serial.print("Periodic beacon: "); Serial.println(periodicEnabled ? "ENABLED" : "DISABLED (silent until DTMF)");
Serial.print("Intro sound: "); Serial.println(useMelody ? "Melody" : "Long tone");
Serial.println("Send 'x' during rest to enter menu.");
}
void loop() {
  // Initial transmission only if periodic is enabled
if (periodicEnabled) {
performTransmission();
  } else {
Serial.println("Silent mode active – waiting for DTMF activation.");
  }
  // Main listening loop (rest or infinite in silent mode)
long startListen = millis();
while (true) {
unsigned long listenDuration = periodicEnabled ? REST_MS : 0x7FFFFFFFUL; // effectively forever
while (millis() - startListen < listenDuration) {
      // Serial menu
if (Serial.available()) {
char c = Serial.read();
if (c == 'x') {
while (Serial.available()) Serial.read();
menu();
          startListen = millis(); // restart timer after menu
continue;
        }
      }
checkSoftwareDTMF();
if (forceImmediateTX) {
        forceImmediateTX = false;
Serial.println("DTMF forced immediate transmission.");
performTransmission();
        startListen = millis(); // full new rest/listen after forced TX
      }
delay(120);
    }
    // Only reach here if periodicEnabled (listenDuration ended)
performTransmission();
    startListen = millis();
  }
}
void performTransmission() {
Serial.println("=== TRANSMITTING ===");
digitalWrite(TX_PIN, HIGH);
if (useMelody) {
  playMelody();
} else {
  playNote(TONE_HZ, LONG_TONE_MS);
}
delay(250);
playCode(morseCode);
digitalWrite(TX_PIN, LOW);
Serial.println("=== TX complete ===");
}
// ====================== TONE / MORSE ======================
void playNote(int freq, long duration) {
if (duration > 0 && freq > 0) {
digitalWrite(LED_BUILTIN, HIGH);
long startTime = millis();
long elapsed = 0;
long period = 1000000L / freq;
while (elapsed < duration) {
digitalWrite(AUDIO_PIN, HIGH);
delayMicroseconds(period / 2);
digitalWrite(AUDIO_PIN, LOW);
delayMicroseconds(period / 2);
      elapsed = millis() - startTime;
    }
digitalWrite(LED_BUILTIN, LOW);
  } else {
delay(DIT_MS * 2);
  }
}
void playMelody() {
  for (int i = 0; i < numNotes; i++) {
    playNote(melody[i], NOTE_DURATION);
  }
}
void playCode(const String& code) {
for (size_t i = 0; i < code.length(); ++i) {
char element = code[i];
long duration_ms = 0;
if (element == '1') duration_ms = DIT_MS;
else if (element == '2') duration_ms = DIT_MS * 3;
playNote(TONE_HZ, duration_ms);
if (element != '0') delay(DIT_MS);
  }
}
long calculateMorseDuration(const String& code) {
long total = 0;
for (size_t i = 0; i < code.length(); ++i) {
char element = code[i];
if (element == '1') total += DIT_MS;
else if (element == '2') total += DIT_MS * 3;
else if (element == '0') total += DIT_MS * 2;
if (element != '0') total += DIT_MS;
  }
return total;
}
long calculateIntroDuration() {
  if (useMelody) {
    return (long)numNotes * NOTE_DURATION;
  } else {
    return LONG_TONE_MS;
  }
}
String formMorse(const String& input) {
  String output = "";
for (size_t i = 0; i < input.length(); ++i) {
char ch = input[i];
if (ch >= 'A' && ch <= 'Z') output += morseTable[ch - 'A' + 1] + '0';
else if (ch >= 'a' && ch <= 'z') output += morseTable[ch - 'a' + 1] + '0';
else if (ch >= '0' && ch <= '9') output += morseTable[ch - '0' + 27] + '0';
else if (ch == ' ') output += morseTable[0];
  }
return output;
}
// ====================== SOFTWARE DTMF ======================
void checkSoftwareDTMF() {
uint8_t tones = dtmfDecoder.detect();
char digit = dtmfDecoder.tone2char(tones);
if (digit != 0) {
    dtmfBuffer += digit;
    dtmfLastDigitTime = millis();
Serial.print("DTMF digit: "); Serial.println(digit);
  }
if (dtmfBuffer.length() > 0 && millis() - dtmfLastDigitTime > DTMF_INTER_DIGIT_TIMEOUT) {
processDTMFCommand(dtmfBuffer);
    dtmfBuffer = "";
  }
}
void processDTMFCommand(String cmd) {
Serial.print("Received DTMF: "); Serial.println(cmd);
if (cmd.endsWith("#")) cmd.remove(cmd.length() - 1);
if (cmd == dtmfActivationCode) {
Serial.println("*** DTMF ACTIVATION! ***");
    forceImmediateTX = true;
  }
}
// ====================== SERIAL MENU ======================
void printMenu() {
Serial.println("\n=== Morse Fox Hunt Transmitter ===");
Serial.println("Version March 02 2026 – with DTMF + Periodic Toggle + Melody Option");
long morseDuration = calculateMorseDuration(morseCode);
long active = calculateIntroDuration() + 250 + morseDuration;
float duty = periodicEnabled ? (static_cast<float>(active) / (active + REST_MS)) * 100.0 : 0.0;
Serial.print("Duty cycle: "); Serial.print(duty, 1); Serial.println("%");
Serial.println("Menu:");
Serial.print("1 - Message ["); Serial.print(message); Serial.println("]");
Serial.print("2 - Tone Hz ["); Serial.print(TONE_HZ); Serial.println("]");
Serial.print("3 - Dit ms ["); Serial.print(DIT_MS); Serial.println("]");
Serial.print("4 - Rest sec ["); Serial.print(REST_MS / 1000); Serial.println("]");
Serial.print("5 - Long tone sec ["); Serial.print(LONG_TONE_MS / 1000); Serial.println("]");
Serial.println("6 - Reset defaults");
Serial.print("7 - DTMF code ["); Serial.print(dtmfActivationCode); Serial.println("]");
Serial.print("8 - Periodic beacon ["); Serial.print(periodicEnabled ? "ON" : "OFF"); Serial.println("]");
Serial.print("9 - Intro sound ["); Serial.print(useMelody ? "Melody" : "Long tone"); Serial.println("]");
Serial.println("x - Exit menu");
}
void menu() {
Serial.println("Entering configuration menu...");
long startMenu = millis();
while (true) {
printMenu();
Serial.print(">");
while (!Serial.available()) {
if (millis() - startMenu > MENU_TIMEOUT_MS) {
Serial.println("\nMenu timeout – resuming.");
return;
      }
delay(10);
    }
char cmd = Serial.read();
if (cmd == '\r' || cmd == '\n') continue;
while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n')) Serial.read();
Serial.println(cmd);
processCommand(cmd);
if (cmd == 'x') {
Serial.println("Exiting menu – resuming operation.");
return;
    }
    startMenu = millis();
  }
}
void processCommand(char cmd) {
  String input;
if (cmd == '1') {
Serial.print("New message: ");
    input = readSerialLine(MESSAGE_TIMEOUT_MS);
if (input.length() > 0 && input.length() <= MAX_MESSAGE_LEN) {
      message = input;
      morseCode = formMorse(message);
writeStringToEEPROM(EEPROM_ADDR_MESSAGE, message);
Serial.println("Message saved.");
    }
  } else if (cmd == '2') { /* tone Hz */
Serial.print("New tone Hz: ");
    input = readSerialLine();
int v = input.toInt();
if (v > 0) {
      TONE_HZ = v; 
EEPROM.put(EEPROM_ADDR_TONE_HZ, TONE_HZ);
Serial.println("Tone updated.");
    }
  } else if (cmd == '3') { /* dit ms */
Serial.print("New dit ms: ");
    input = readSerialLine();
int v = input.toInt();
if (v > 0) {
      DIT_MS = v;
EEPROM.put(EEPROM_ADDR_DIT_MS, DIT_MS);
Serial.println("Dit updated.");
    }
  } else if (cmd == '4') { /* rest sec */
Serial.print("New rest seconds: ");
    input = readSerialLine();
long v = input.toInt() * 1000L;
if (v > 0) {
      REST_MS = v;
EEPROM.put(EEPROM_ADDR_REST_MS, REST_MS);
Serial.println("Rest updated.");
    }
  } else if (cmd == '5') { /* long tone */
Serial.print("New long tone seconds: ");
    input = readSerialLine();
long v = input.toInt() * 1000L;
if (v > 0) {
      LONG_TONE_MS = v;
EEPROM.put(EEPROM_ADDR_LONG_TONE_MS, LONG_TONE_MS);
Serial.println("Long tone updated.");
    }
  } else if (cmd == '6') {
resetToDefaults();
Serial.println("All defaults restored.");
  } else if (cmd == '7') {
Serial.print("New DTMF code (max 8 chars): ");
    input = readSerialLine(MESSAGE_TIMEOUT_MS);
if (input.length() > 0 && input.length() <= 8) {
      dtmfActivationCode = input;
writeStringToEEPROM(EEPROM_ADDR_DTMF_CODE, dtmfActivationCode);
Serial.println("DTMF code saved.");
    }
  } else if (cmd == '8') { // NEW
    periodicEnabled = !periodicEnabled;
    byte p = periodicEnabled ? 1 : 0;
EEPROM.put(EEPROM_ADDR_PERIODIC, p);
Serial.print("Periodic beaconing is now ");
Serial.println(periodicEnabled ? "ENABLED" : "DISABLED");
if (!periodicEnabled) Serial.println("(Fox will stay silent until DTMF received)");
  } else if (cmd == '9') { // NEW: Toggle intro sound
    useMelody = !useMelody;
    byte u = useMelody ? 1 : 0;
EEPROM.put(EEPROM_ADDR_USE_MELODY, u);
Serial.print("Intro sound is now ");
Serial.println(useMelody ? "Melody" : "Long tone");
  } else if (cmd == 'x') {
    // handled in menu()
  } else {
Serial.println("Unknown command.");
  }
}
// ====================== SERIAL INPUT ======================
String readSerialLine(long timeoutMs, bool printTimeoutMsg) {
  String line = "";
long start = millis();
while (millis() - start < timeoutMs) {
if (Serial.available()) {
char c = Serial.read();
if (c == '\r') {
Serial.println();
return line;
      } else if (c == '\n') {
        // ignore
      } else if (c == '\b' || c == 0x7F) {
if (line.length() > 0) {
          line = line.substring(0, line.length() - 1);
Serial.print("\b \b");
        }
      } else {
        line += c;
Serial.print(c);
      }
      start = millis();
    }
delay(10);
  }
if (printTimeoutMsg) Serial.println("\nInput timeout.");
return "";
}
void writeStringToEEPROM(int addr, const String& str) {
  byte len = str.length();
EEPROM.write(addr, len);
for (byte i = 0; i < len; i++) EEPROM.write(addr + 1 + i, str[i]);
EEPROM.write(addr + 1 + len, 0);
}
String readStringFromEEPROM(int addr) {
  String str = "";
  byte len = EEPROM.read(addr);
if (len > MAX_MESSAGE_LEN) len = MAX_MESSAGE_LEN;
for (byte i = 0; i < len; i++) str += (char)EEPROM.read(addr + 1 + i);
return str;
}
void resetToDefaults() {
  TONE_HZ = 600;
  DIT_MS = 64;
  REST_MS = 180000;
  LONG_TONE_MS = 10000;
  message = "21M085 FOX 21M085 FOX";
  dtmfActivationCode = "1985";
  periodicEnabled = true;
  useMelody = false;
  morseCode = formMorse(message);
EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
EEPROM.put(EEPROM_ADDR_TONE_HZ, TONE_HZ);
EEPROM.put(EEPROM_ADDR_DIT_MS, DIT_MS);
EEPROM.put(EEPROM_ADDR_REST_MS, REST_MS);
EEPROM.put(EEPROM_ADDR_LONG_TONE_MS, LONG_TONE_MS);
writeStringToEEPROM(EEPROM_ADDR_MESSAGE, message);
writeStringToEEPROM(EEPROM_ADDR_DTMF_CODE, dtmfActivationCode);
EEPROM.put(EEPROM_ADDR_PERIODIC, (byte)1);
EEPROM.put(EEPROM_ADDR_USE_MELODY, (byte)0);
}
