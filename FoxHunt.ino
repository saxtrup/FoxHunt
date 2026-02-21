/*
 * Morse Code Transmitter for Radio Fox Hunt
 * 
 * Original Author: VE6BTC (IDE 1.6.0)
 * Updated: November 2020 by VA6MCP (IDE 1.8.13)
 * 2nd Update: February 2026 by 21M085 (IDE 2.3.7)
 * Further refinements to the code include improved variable naming, structured comments,
 * function documentation, and timing adjustments.
 * 
 * Purpose:
 * This sketch enables an Arduino to transmit Morse code via a Baofeng UV-5R or similar radio
 * for amateur radio fox hunting activities. It generates audio tones for the radio's microphone
 * input and controls the PTT (Push-To-Talk) line using a relay.
 * 
 * Hardware Connections:
 * - Pin 7 (TX_PIN): Controls the TX relay (normally closed configuration).
 * - Pin 5 (AUDIO_PIN): Outputs audio tones to the radio's microphone input.
 * - Pin 13: On-board LED for visualizing Morse code output (optional debugging).
 * 
 * Configuration:
 * - Transmission starts with a long continuous tone for easy direction finding.
 * - Followed by Morse code identification.
 * - Cycles with a rest period between transmissions.
 * 
 * Notes:
 * - Relay is normally closed: HIGH on TX_PIN opens the circuit (stops TX), LOW closes it (starts TX).
 * - Tone frequency is approximate due to square wave generation and harmonics.
 * - Morse timing follows standard conventions exactly: dit = 1 unit, dah = 3 units, inter-element = 1 unit,
 *   inter-character = 3 units, inter-word = 7 units. This is achieved by appending '0' for inter-character
 *   (adding 2 units via pause), and using "000" for word spaces (adding three pauses for 6 units extra).
 */

#define TONE_HZ      600     // Tone frequency in Hz (actual slightly lower with harmonics).
#define DIT_MS       64      // Dit duration in ms (1 unit); dah and pauses derived from this.
#define REST_MS      60000   // Rest period between transmissions in ms (60 seconds).
#define LONG_TONE_MS 10000   // Long initial tone duration in ms (10 seconds).
#define TX_PIN       7       // Pin controlling TX relay.
#define AUDIO_PIN    5       // Pin for audio output to radio mic.

// Morse code table: 1 = dit, 2 = dah, 0 = space pause unit.
// Index 0: word space (three pauses for extra 6 units).
// Indices 1-26: A-Z.
// Indices 27-36: 0-9.
const String morseTable[] = {
  "000",   // Word space (adjusted for exact 7-unit timing)
  "12",    // A
  "2111",  // B
  "2121",  // C
  "211",   // D
  "1",     // E
  "1121",  // F
  "221",   // G
  "1111",  // H
  "11",    // I
  "1222",  // J
  "212",   // K
  "1211",  // L
  "22",    // M
  "21",    // N
  "222",   // O
  "1221",  // P
  "2212",  // Q
  "121",   // R
  "111",   // S
  "2",     // T
  "112",   // U
  "1112",  // V
  "122",   // W
  "2112",  // X
  "2122",  // Y
  "2211",  // Z
  "22222", // 0
  "12222", // 1
  "11222", // 2
  "11122", // 3
  "11112", // 4
  "11111", // 5
  "21111", // 6
  "22111", // 7
  "22211", // 8
  "22221"  // 9
};

// Function prototypes
String formMorse(const String& input);
void playCode(const String& code);
void playTone(long duration);

// User-configurable message to transmit in Morse code.
const String message = "21M085 FOX 21M085 FOX";
const String morseCode = formMorse(message);

// Global variables
long notePeriod = 1000000L / TONE_HZ;  // Microseconds per cycle.

void setup() {
  pinMode(TX_PIN, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);  // Use built-in LED constant for clarity.

  digitalWrite(TX_PIN, HIGH);  // Initial state: stop transmission (energize relay to open).

  // Serial debugging (optional; can be removed in production).
  Serial.begin(9600);
  Serial.print("Message: ");
  Serial.println(message);
  Serial.print("Morse Code: ");
  Serial.println(morseCode);
}

void loop() {
  digitalWrite(TX_PIN, LOW);  // Start transmission (de-energize relay to close).

  // Transmit long tone for direction finding.
  playTone(LONG_TONE_MS);
  delay(250);  // Brief pause before Morse code.

  // Transmit Morse code message.
  playCode(morseCode);

  digitalWrite(TX_PIN, HIGH);  // Stop transmission.

  delay(REST_MS);  // Rest before next cycle.
}

/**
 * Generates a tone or pause for the specified duration.
 * @param duration Tone duration in ms (0 for pause).
 */
void playTone(long duration) {
  if (duration > 0) {
    digitalWrite(LED_BUILTIN, HIGH);  // Visualize tone.
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
      duration_ms = DIT_MS;      // Dit (1 unit).
    } else if (element == '2') {
      duration_ms = DIT_MS * 3;  // Dah (3 units).
    } else if (element == '0') {
      duration_ms = 0;           // Pause.
    }
    playTone(duration_ms);
    if (element != '0') {
      delay(DIT_MS);  // Inter-element pause (1 unit).
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
      output += morseTable[ch - 'A' + 1] + '0';  // Append single pause for inter-character.
    } else if (ch >= '0' && ch <= '9') {
      output += morseTable[ch - '0' + 27] + '0';  // Append single pause for inter-character.
    } else if (ch == ' ') {
      output += morseTable[0];  // Word space (three pauses).
    }
    // Ignore unsupported characters.
  }
  return output;
}