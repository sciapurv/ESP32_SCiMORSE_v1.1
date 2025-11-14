/*
  SCiMORSE v1.1
  ESP32 (30-pin CP2102)
  Pins:
    OLED SDA  -> 21
    OLED SCL  -> 22
    BTN UP    -> 32
    BTN DOWN  -> 33
    BTN OK    -> 25
    BTN BACK  -> 26
    BTN HOME  -> 27
    POT (WPM) -> 34 (ADC)
    BUZZER    -> 19
    LED (dot/dash) -> 23
  WiFi AP:
    SSID: SCiMORSE
    PASS: sciapurv
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* ------------------ CONFIG ------------------ */
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Buttons (wired to GND, use INPUT_PULLUP)
#define BTN_UP_PIN    32
#define BTN_DOWN_PIN  33
#define BTN_OK_PIN    25
#define BTN_BACK_PIN  26
#define BTN_HOME_PIN  27

#define POT_PIN 34      // ADC1_6

#define BUZZER_PIN 19
#define LED_PIN 23

// WiFi AP
const char *AP_SSID = "SCiMORSE";
const char *AP_PASS = "sciapurv";

WebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ------------------ UI STATE ------------------ */
/* Added new practice pages to enum */
enum Page { P_HOME, P_MENU, P_TRANSMIT, P_PRACTICE_MENU, P_PRACTICE_START, P_PRACTICE_HOW, P_PRACTICE_CODES, P_SETTINGS, P_ABOUT };
Page page = P_HOME;

// menu cursor in main menu
int menuIndex = 0; // 0=start,1=practice,2=settings,3=about

// practice submenu cursor
int practiceMenuIndex = 0; // 0=Start Practice,1=How to Play,2=Morse Codes

// settings cursor
int settingsCursor = 0; // 0=buzzer,1=led,2=save/back

/* ------------------ SETTINGS ------------------ */
volatile int currentWPM = 20;     // 5..40 (updated from pot)
bool buzzerEnabled = true;
bool ledEnabled = true;

/* ------------------ MORSE TX ------------------ */
String txPlain = "";     // original text
String txMorse = "";     // encoded morse ('.' '-' ' ' '/')
size_t txIndex = 0;
unsigned long dotMs = 0; // ms duration of dot

enum TxPhase { TX_IDLE, TX_ELEMENT_ON, TX_ELEMENT_OFF, TX_BETWEEN_LETTERS, TX_BETWEEN_WORDS };
TxPhase txPhase = TX_IDLE;
unsigned long nextTransition = 0;

/* ------------------ Buttons ------------------ */
unsigned long lastButtonCheck = 0;
const unsigned long BUTTON_POLL_MS = 30;

bool btnUpPressed=false, btnDownPressed=false, btnOkPressed=false, btnBackPressed=false, btnHomePressed=false;
bool lastUpState=false, lastDownState=false, lastOkState=false, lastBackState=false, lastHomeState=false;

/* ------------------ Display timing ------------------ */
unsigned long lastDisplayUpdate = 0;

/* ------------------ Morse tables ------------------ */
const char* letters[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..",
  ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.",
  "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."
};
const char* numbers[] = {
  "-----", ".----", "..---", "...--", "....-", ".....",
  "-....", "--...", "---..", "----."
};

/* ------------------ PRACTICE MODE STATE ------------------ */
char practiceTarget = 0;            // single character target (A-Z or 0-9)
String practiceInput = "";          // user's entered morse for current trial (uses '.' '-' '/')
unsigned long practiceMsgUntil = 0; // if > millis() then message is on-screen
String practiceMessage = "";        // "Good Guess,\nTry Again!" or "Great,\nOne More!"
bool practiceShowingMsg = false;
int practiceCodesIndex = 0;         // index for Morse Codes scroll
const int PRACTICE_TOTAL_CODES = 26 + 10; // A-Z + 0-9

/* ------------------ PROTOTYPES ------------------ */
void updateDotMs();
String encodeToMorse(const String &text);
void startTransmission(const String &text);
void handleTxState();
void readButtons();
void uiHandleButtons();
void updateDisplay();
void serveRoot();
void serveSend();
void serveStatus();
void serveToggle();
void toneStart();
void toneStop();
String morseForChar(char c);
void pickNewPracticeTarget();
void resetPracticeInput();

/* ------------------ Buzzer helpers (digital HIGH/LOW) ------------------ */
void toneStart() {
  digitalWrite(BUZZER_PIN, HIGH);
}
void toneStop() {
  digitalWrite(BUZZER_PIN, LOW);
}

/* ------------------ SETUP ------------------ */
void setupPins() {
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_OK_PIN, INPUT_PULLUP);
  pinMode(BTN_BACK_PIN, INPUT_PULLUP);
  pinMode(BTN_HOME_PIN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(50);

  setupPins();

  // OLED init
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 init failed");
    // continue but display will not work
  } else {
    display.clearDisplay();
  }

  // Boot screens
  display.clearDisplay();
display.setTextSize(2);
display.setTextColor(SSD1306_WHITE);
display.setCursor(16, 24);   // Centered for 128x64, text size 2
display.print("SCiMORSE");
display.display();
delay(1000);


  for (int i=0;i<3;i++){
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(31,28);
    display.print("Configuring");
    for (int j=0;j<=i;j++) display.print(".");
    display.display();
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }

  toneStart();
  delay(300);
  toneStop();

  display.clearDisplay();
  display.setCursor(31,28);
  display.print("Done!");
  display.display();
  delay(500);

  // WiFi AP
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(myIP);

  // server routes
  server.on("/", HTTP_GET, serveRoot);
  server.on("/send", HTTP_GET, serveSend);
  server.on("/status", HTTP_GET, serveStatus);
  server.on("/toggle", HTTP_GET, serveToggle);
  server.begin();

  // init values
  currentWPM = 20;
  updateDotMs();

  // practice seed and initial pick
  randomSeed(analogRead(POT_PIN));
  pickNewPracticeTarget();

  // start on Home
  page = P_HOME;
  menuIndex = 0;
  practiceMenuIndex = 0;
  settingsCursor = 0;
}

/* ------------------ HELPERS ------------------ */
void updateDotMs(){
  int w = currentWPM;
  if (w < 5) w = 5;
  if (w > 40) w = 40;
  dotMs = 1200UL / (unsigned long)w; // standard PARIS method approx
}

String encodeToMorse(const String &text) {
  String out = "";
  for (size_t i=0;i<text.length(); i++){
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if (c >= 'a' && c <= 'z') {
      const char *code = letters[c - 'a'];
      if (out.length()>0 && out[out.length()-1] != '/') out += ' ';
      out += String(code);
    } else if (c >= '0' && c <= '9') {
      const char *code = numbers[c - '0'];
      if (out.length()>0 && out[out.length()-1] != '/') out += ' ';
      out += String(code);
    } else if (c == ' ') {
      if (out.length()>0 && out[out.length()-1] != '/') out += '/';
    }
  }
  return out;
}

/* Return morse string for single character (letters or numbers). Returns "" if not recognized */
String morseForChar(char c) {
  if (c >= 'A' && c <= 'Z') {
    return String(letters[c - 'A']);
  } else if (c >= 'a' && c <= 'z') {
    return String(letters[c - 'a']);
  } else if (c >= '0' && c <= '9') {
    return String(numbers[c - '0']);
  }
  return String("");
}

/* pick random single char target (letter A-Z or number 0-9) */
void pickNewPracticeTarget() {
  int total = 26 + 10;
  int r = random(total); // 0..35
  if (r < 26) {
    practiceTarget = 'A' + r;
  } else {
    practiceTarget = '0' + (r - 26);
  }
  resetPracticeInput();
}

/* clear user input */
void resetPracticeInput() {
  practiceInput = "";
  practiceShowingMsg = false;
  practiceMsgUntil = 0;
  practiceMessage = "";
}

/* ------------------ TRANSMIT ------------------ */
void startTransmission(const String &text) {
  if (text.length() == 0) return;
  txPlain = text;
  txMorse = encodeToMorse(text);
  txIndex = 0;
  txPhase = TX_IDLE;
  nextTransition = millis() + 50;
  Serial.print("TX start: "); Serial.println(txMorse);
  // when transmission starts, go to transmit page
  page = P_TRANSMIT;
}

/* ------------------ Transmission state machine (non-blocking) ------------------ */
void handleTxState() {
  unsigned long now = millis();
  if (txMorse.length() == 0) return;

  switch (txPhase) {
    case TX_IDLE:
      if (txIndex < txMorse.length()) {
        char ch = txMorse[txIndex];
        if (ch == '.') {
          if (ledEnabled) digitalWrite(LED_PIN, HIGH);
          if (buzzerEnabled) toneStart();
          txPhase = TX_ELEMENT_ON;
          nextTransition = now + dotMs;
        } else if (ch == '-') {
          if (ledEnabled) digitalWrite(LED_PIN, HIGH);
          if (buzzerEnabled) toneStart();
          txPhase = TX_ELEMENT_ON;
          nextTransition = now + dotMs * 3;
        } else if (ch == ' ') {
          txPhase = TX_BETWEEN_LETTERS;
          nextTransition = now + dotMs * 3;
          txIndex++;
        } else if (ch == '/') {
          txPhase = TX_BETWEEN_WORDS;
          nextTransition = now + dotMs * 7;
          txIndex++;
        } else {
          txIndex++;
        }
      } else {
        // finished
        txMorse = "";
        txPlain = "";
        txIndex = 0;
        txPhase = TX_IDLE;
        digitalWrite(LED_PIN, LOW);
        toneStop();
        Serial.println("TX done");
      }
      break;

    case TX_ELEMENT_ON:
      if (now >= nextTransition) {
        if (ledEnabled) digitalWrite(LED_PIN, LOW);
        if (buzzerEnabled) toneStop();
        txPhase = TX_ELEMENT_OFF;
        nextTransition = now + dotMs; // intra-element gap
        txIndex++;
      }
      break;

    case TX_ELEMENT_OFF:
      if (now >= nextTransition) {
        txPhase = TX_IDLE;
      }
      break;

    case TX_BETWEEN_LETTERS:
      if (now >= nextTransition) {
        txPhase = TX_IDLE;
      }
      break;

    case TX_BETWEEN_WORDS:
      if (now >= nextTransition) {
        txPhase = TX_IDLE;
      }
      break;
  }
}

/* ------------------ Buttons & UI handling ------------------ */
void readButtons() {
  unsigned long now = millis();
  if (now - lastButtonCheck < BUTTON_POLL_MS) return;
  lastButtonCheck = now;

  bool up = digitalRead(BTN_UP_PIN) == LOW;
  bool down = digitalRead(BTN_DOWN_PIN) == LOW;
  bool ok = digitalRead(BTN_OK_PIN) == LOW;
  bool back = digitalRead(BTN_BACK_PIN) == LOW;
  bool homeBtn = digitalRead(BTN_HOME_PIN) == LOW;

  if (up && !lastUpState) btnUpPressed = true;
  if (down && !lastDownState) btnDownPressed = true;
  if (ok && !lastOkState) btnOkPressed = true;
  if (back && !lastBackState) btnBackPressed = true;
  if (homeBtn && !lastHomeState) btnHomePressed = true;

  lastUpState = up;
  lastDownState = down;
  lastOkState = ok;
  lastBackState = back;
  lastHomeState = homeBtn;
}

void uiHandleButtons() {
  unsigned long now = millis();

  // If practice message is being shown, ignore input until it's over
  if (practiceShowingMsg && now < practiceMsgUntil) {
    // still showing message; only handle BACK to exit to practice menu perhaps
    if (btnBackPressed) {
      // allow user to back out to practice menu while message is displayed
      page = P_PRACTICE_MENU;
      btnBackPressed = false;
      practiceShowingMsg = false;
      practiceMsgUntil = 0;
    }
    // clear button flags
    btnUpPressed = btnDownPressed = btnOkPressed = btnHomePressed = false;
    return;
  } else if (practiceShowingMsg && now >= practiceMsgUntil) {
    // message ended
    practiceShowingMsg = false;
    practiceMsgUntil = 0;
    // if last result was correct we already picked a new target in code below
  }

  // HOME button general: returns to Home (but if in Start Practice, HOME used as input, so special case)
  if (btnHomePressed) {
    // when in P_PRACTICE_START, HOME is used as 'space' input — so only trigger Home navigation elsewhere
    if (page != P_PRACTICE_START) {
      page = P_HOME;
      btnHomePressed = false;
      return;
    }
    // else leave it to practice input handler below
  }

  // BACK: go to menu / previous
  if (btnBackPressed) {
    if (page == P_SETTINGS || page == P_TRANSMIT || page == P_ABOUT) {
      page = P_MENU;
    } else if (page == P_PRACTICE_MENU) {
      page = P_MENU;
    } else if (page == P_PRACTICE_START || page == P_PRACTICE_HOW || page == P_PRACTICE_CODES) {
      // back to practice submenu
      page = P_PRACTICE_MENU;
    } else {
      page = P_HOME;
    }
    btnBackPressed = false;
  }

  // UP/DOWN/OK behavior depends on current page
  if (page == P_HOME) {
    // Home: OK opens menu
    if (btnOkPressed) {
      page = P_MENU;
      // start menuIndex at 0
      menuIndex = 0;
      btnOkPressed = false;
      return;
    }
    // UP/DOWN do nothing on home
    if (btnUpPressed) btnUpPressed = false;
    if (btnDownPressed) btnDownPressed = false;
  }
  else if (page == P_MENU) {
    if (btnUpPressed) {
      menuIndex--;
      if (menuIndex < 0) menuIndex = 3;
      btnUpPressed = false;
    }
    if (btnDownPressed) {
      menuIndex++;
      if (menuIndex > 3) menuIndex = 0;
      btnDownPressed = false;
    }
    if (btnOkPressed) {
      // select menu item
      if (menuIndex == 0) page = P_TRANSMIT;
      else if (menuIndex == 1) {
        page = P_PRACTICE_MENU;
        practiceMenuIndex = 0;
      }
      else if (menuIndex == 2) {
        page = P_SETTINGS;
        settingsCursor = 0;
      }
      else if (menuIndex == 3) page = P_ABOUT;
      btnOkPressed = false;
    }
  }
  else if (page == P_PRACTICE_MENU) {
    // practice submenu navigation
    if (btnUpPressed) {
      practiceMenuIndex--;
      if (practiceMenuIndex < 0) practiceMenuIndex = 2;
      btnUpPressed = false;
    }
    if (btnDownPressed) {
      practiceMenuIndex++;
      if (practiceMenuIndex > 2) practiceMenuIndex = 0;
      btnDownPressed = false;
    }
    if (btnOkPressed) {
      if (practiceMenuIndex == 0) {
        // Start Practice
        page = P_PRACTICE_START;
        resetPracticeInput();
        pickNewPracticeTarget();
      } else if (practiceMenuIndex == 1) {
        // How to Play
        page = P_PRACTICE_HOW;
      } else if (practiceMenuIndex == 2) {
        // Morse Codes (scrollable by pot)
        page = P_PRACTICE_CODES;
        practiceCodesIndex = 0;
      }
      btnOkPressed = false;
    }
  }
  else if (page == P_PRACTICE_START) {
    // In practice start: buttons change meaning (only here)
    if (btnUpPressed) {
      // append dit
      practiceInput += ".";
      // optional feedback: blink LED/buzzer
      if (ledEnabled) digitalWrite(LED_PIN, HIGH);
      if (buzzerEnabled) toneStart();
      delay(80);
      if (ledEnabled) digitalWrite(LED_PIN, LOW);
      if (buzzerEnabled) toneStop();
      btnUpPressed = false;
    }
    if (btnDownPressed) {
      // append dash
      practiceInput += "-";
      if (ledEnabled) digitalWrite(LED_PIN, HIGH);
      if (buzzerEnabled) toneStart();
      delay(240);
      if (ledEnabled) digitalWrite(LED_PIN, LOW);
      if (buzzerEnabled) toneStop();
      btnDownPressed = false;
    }
    if (btnHomePressed) {
      // append separator (user requested HOME for space)
      practiceInput += "/";
      btnHomePressed = false;
    }
    if (btnOkPressed) {
      // "Send" - check entered morse against target
      String expected = morseForChar(practiceTarget);
      // compare exact match
      if (practiceInput.equals(expected)) {
        // correct
        practiceMessage = "Great,\nOne More!";
        practiceShowingMsg = true;
        practiceMsgUntil = millis() + 2000;
        // pick next target (after showing message)
        pickNewPracticeTarget();
      } else {
        // incorrect
        practiceMessage = "Good Guess,\nTry Again!";
        practiceShowingMsg = true;
        practiceMsgUntil = millis() + 2000;
        // keep same target; clear input so user re-enters
        practiceInput = "";
      }
      btnOkPressed = false;
    }
    // allow BACK handled earlier; HOME input handled as above
  }
  else if (page == P_PRACTICE_HOW) {
    // just one screen; OK returns to practice menu
    if (btnOkPressed) {
      page = P_PRACTICE_MENU;
      btnOkPressed = false;
    }
    if (btnUpPressed || btnDownPressed) {
      page = P_PRACTICE_MENU;
      btnUpPressed = btnDownPressed = false;
    }
  }
else if (page == P_PRACTICE_CODES) {
  // improved pot handling for practice codes
  int raw = analogRead(POT_PIN);

  // map with float math for smoother scrolling
  const int maxIdx = PRACTICE_TOTAL_CODES - 1;
  float proportion = (float)raw / 4095.0f;
  int idx = (int)round(proportion * maxIdx);

  // safety clamp
  if (idx < 0) idx = 0;
  if (idx > maxIdx) idx = maxIdx;

  // hysteresis: only update when index actually changes
  static int lastPotIdx = -1;
  if (idx != lastPotIdx) {
    practiceCodesIndex = idx;
    lastPotIdx = idx;
  }

  // OK button returns to Practice Menu
  if (btnOkPressed) {
    page = P_PRACTICE_MENU;
    btnOkPressed = false;
  }
}
  else if (page == P_SETTINGS) {
    if (btnUpPressed) {
      settingsCursor--;
      if (settingsCursor < 0) settingsCursor = 2;
      btnUpPressed = false;
    }
    if (btnDownPressed) {
      settingsCursor++;
      if (settingsCursor > 2) settingsCursor = 0;
      btnDownPressed = false;
    }
    if (btnOkPressed) {
      if (settingsCursor == 0) buzzerEnabled = !buzzerEnabled;
      else if (settingsCursor == 1) ledEnabled = !ledEnabled;
      else if (settingsCursor == 2) {
        // Save & Back: simply go back to menu
        page = P_MENU;
      }
      btnOkPressed = false;
    }
  }
  else if (page == P_TRANSMIT) {
    // In transmit page OK does nothing; BACK/ HOME handled earlier
    btnUpPressed = btnDownPressed = btnOkPressed = false;
  }
  else if (page == P_ABOUT) {
    if (btnOkPressed) {
      page = P_MENU;
      btnOkPressed = false;
    }
    if (btnUpPressed || btnDownPressed) {
      // navigate back to menu
      page = P_MENU;
      btnUpPressed = btnDownPressed = false;
    }
  }
}

/* ------------------ Display drawing ------------------ */
void updateDisplay() {
  unsigned long now = millis();
  if (now - lastDisplayUpdate < 120) return;
  lastDisplayUpdate = now;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (page == P_HOME) {
    display.setCursor(28,0);
    display.setTextSize(1);
    display.print("> SCiMORSE <");
    display.setCursor(0,12);
    display.print(" ");
    display.setCursor(13,22);
    display.print("Decode, Learn And");
    display.setCursor(37,34);
    display.print("Transmit!");
    display.setCursor(0,52);
    display.print("[OK] for Menu");
  }
  else if (page == P_MENU) {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("Menu:");
    display.setCursor(0,16);
    if (menuIndex == 0) display.print("> Start Converter");
    else display.print("  Start Converter");
    display.setCursor(0,30);
    if (menuIndex == 1) display.print("> Practice Mode");
    else display.print("  Practice Mode");
    display.setCursor(0,44);
    if (menuIndex == 2) display.print("> Settings");
    else display.print("  Settings");
    display.setCursor(0,56);
    if (menuIndex == 3) display.print("> About");
    else display.print("  About");
    // note: instructions on line overflow are avoided by using separate lines
  }
  else if (page == P_TRANSMIT) {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("Transmitting:");
    display.setCursor(0,12);
    if (txPlain.length() == 0) {
      display.print("Waiting for input...");
      display.setCursor(0,28);
      display.print("(Use Webpage)");
    } else {
      display.print(txPlain.substring(0, min(24, (int)txPlain.length())));
      display.setCursor(0,28);
      display.print("WPM: ");
      display.print(currentWPM);
      display.setCursor(0,40);
      display.print("Morse:");
      display.setCursor(0,52);
      if (txMorse.length() > 0) display.print(txMorse.substring(0, min(24, (int)txMorse.length())));
      else display.print("done");
    }
  }
  else if (page == P_PRACTICE_MENU) {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("Practice Mode:");
    display.setCursor(0,16);
    if (practiceMenuIndex == 0) display.print("> Start Practice");
    else display.print("  Start Practice");
    display.setCursor(0,30);
    if (practiceMenuIndex == 1) display.print("> How to Play?");
    else display.print("  How to Play?");
    display.setCursor(0,44);
    if (practiceMenuIndex == 2) display.print("> Morse Codes");
    else display.print("  Morse Codes");
    display.setCursor(0, 56);
    display.print("[OK]           [BACK]");
  }
else if (page == P_PRACTICE_START) {
  display.setTextSize(1);
  // If a practice message is active (Good Guess / Great), show it centered
  if (practiceShowingMsg) {
    // center text - two lines
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor((SCREEN_WIDTH - 6 * 12) / 2, 18); // approximate centering
    display.print(practiceMessage.substring(0, min(12, (int)practiceMessage.length()))); // first line

    // second line
    int nl = practiceMessage.indexOf('\n');
    if (nl >= 0) {
      String second = practiceMessage.substring(nl + 1);
      display.setCursor((SCREEN_WIDTH - 6 * 12) / 2, 32);
      display.print(second);
    }
  }
  else {
    // --- Normal practice screen (clean layout) ---
    display.clearDisplay();
    display.setTextSize(1);

    // Target letter
    display.setCursor(0, 8);
    display.print("Target: ");
    display.println(practiceTarget);

    // Entered Morse
    display.setCursor(0, 24);
    display.print("Enter: ");
    display.println(practiceInput);  // show what user typed live

    // Bottom navigation
    display.setCursor(0, 48);
    display.print("[OK]        BACK:Exit");
  }
}
else if (page == P_PRACTICE_HOW) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("How to Play?");
  display.setCursor(0,15);
  display.print("1. [UP] for Dit");
  display.setCursor(0,28);
  display.print("2. [DOWN] for Dash");
  display.setCursor(0,40);
  display.print("3. [HOME] for Space");
  display.setCursor(0,52);
  display.print("4. [OK] for Send");
}
  else if (page == P_PRACTICE_CODES) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Rotate For Scroll:");
  display.println();

  int startIndex = (practiceCodesIndex / 3) * 3;  // group of 3 per page
  int cursorPos = practiceCodesIndex % 3;         // which of the 3 is selected

  for (int i = 0; i < 3; i++) {
    int idx = startIndex + i;
    if (idx >= PRACTICE_TOTAL_CODES) break;

    char c = (idx < 26) ? 'A' + idx : '0' + (idx - 26);
    if (i == cursorPos) display.print("> ");
    else display.print("  ");
    display.print(c);
    display.print(" = ");
    display.println(morseForChar(c));
  }

  display.println();
  display.println("[OK]");
  display.display();
}
  else if (page == P_SETTINGS) {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("Settings:");
    display.setCursor(0,12);
    display.print("WPM (Rotate): ");
    display.print(currentWPM);
    // show two toggles with cursor
    display.setCursor(0,28);
    if (settingsCursor == 0) display.print("> Buzzer: ");
    else display.print("  Buzzer: ");
    display.print(buzzerEnabled ? "On" : "Off");
    display.setCursor(0,40);
    if (settingsCursor == 1) display.print("> LED: ");
    else display.print("  LED: ");
    display.print(ledEnabled ? "On" : "Off");
    display.setCursor(0,52);
    if (settingsCursor == 2) display.print("> Save & Back");
    else display.print("  Save & Back");
  }
  else if (page == P_ABOUT) {
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("SCiMORSE v1.1");
    display.setCursor(0,14);
    display.print("Made By @sciapurv");
    display.setCursor(0,26);
    display.print("Webpage: 192.168.4.1");
    display.setCursor(0,38);
    display.print("Wifi Pass: ");
    display.print(AP_PASS);  // "Your Password"
    display.display();
    display.setCursor(0,53);
    display.print("[BACK]");
  }

  display.display();
}

/* ------------------ Web server: page + handlers ------------------ */
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SCiMORSE</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;padding:12px}
.container{max-width:480px;margin:0 auto;background:#fff;padding:12px;border-radius:8px}
h1{font-size:20px}
textarea{width:100%;height:80px;font-size:16px}
button{padding:10px 14px;font-size:16px}
.row{display:flex;gap:8px;align-items:center}
.small{font-size:13px;color:#333}
</style>
</head>
<body>
<div class="container">
  <h1>SCiMORSE v1.1</h1>
  <div class="small">SCiMORSE is a fun ESP32-based Morse Code Trainer that lets you learn, send, and decode Morse using buttons, buzzer, OLED display, and a web interface at 192.168.4.1.
  Made By @sciapurv (YouTube/Instagram)</div>
  <h3>Text to Morse code!</h3>
  <form action="/send" method="get">
    <textarea name="msg" placeholder="Type message (A-Z,0-9)..."></textarea>
    <div class="row"><button type="submit">Send</button></div>
  </form>
  <h3>Settings</h3>
  <div class="row">
    Buzzer: <button onclick="toggle('buzzer');return false" id="bbtn">Switch</button>
    &nbsp;&nbsp;
    LED: <button onclick="toggle('led');return false" id="lbtn">Switch</button>
  </div>
  <h3>Status</h3>
  <div id="status">Loading...</div>
</div>
<script>
async function fetchJSON(path){ let r=await fetch(path); return r.json(); }
async function refresh(){
  try {
    const s = await fetchJSON('/status');
    document.getElementById('status').innerText = 'WPM: ' + s.wpm + ' | Tx: ' + (s.tx ? s.txmsg : 'Idle') + ' | Buzzer: ' + (s.buzzer? 'On':'Off') + ' | LED: ' + (s.led? 'On':'Off');
  } catch(e){ document.getElementById('status').innerText = 'Disconnected'; }
}
setInterval(refresh,1500);
refresh();

async function toggle(name){
  await fetch('/toggle?name=' + name);
  setTimeout(refresh,300);
}
</script>
</body></html>
)rawliteral";

void serveRoot() {
  server.send_P(200, "text/html", index_html);
}

void serveSend() {
  if (!server.hasArg("msg")) {
    server.send(400, "text/plain", "Missing msg");
    return;
  }
  String msg = server.arg("msg");
  startTransmission(msg);
  server.send(200, "text/html", "<html><body><h2>Message Sent Successfully!</h2><a href='/'>[BACK]</a></body></html>");
}

void serveStatus() {
  String payload = "{";
  payload += "\"wpm\":" + String(currentWPM) + ",";
  payload += "\"buzzer\":" + String(buzzerEnabled ? 1 : 0) + ",";
  payload += "\"led\":" + String(ledEnabled ? 1 : 0) + ",";
  if (txPlain.length() > 0) {
    payload += "\"tx\":1,";
    payload += "\"txmsg\":\"" + txPlain + "\"";
  } else {
    payload += "\"tx\":0,";
    payload += "\"txmsg\":\"\"";
  }
  payload += "}";
  server.send(200, "application/json", payload);
}

void serveToggle() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "Missing"); return; }
  String name = server.arg("name");
  if (name == "buzzer") buzzerEnabled = !buzzerEnabled;
  else if (name == "led") ledEnabled = !ledEnabled;
  server.send(200, "text/plain", "OK");
}

/* ------------------ MAIN LOOP ------------------ */
void loop() {
  // web
  server.handleClient();

  // read pot
  int raw = analogRead(POT_PIN);

  // If we're in Morse Codes screen, use pot to scroll codes.
  if (page == P_PRACTICE_CODES) {
    int idx = map(raw, 0, 4095, 0, PRACTICE_TOTAL_CODES - 1);
    if (idx < 0) idx = 0;
    if (idx >= PRACTICE_TOTAL_CODES) idx = PRACTICE_TOTAL_CODES - 1;
    if (idx != practiceCodesIndex) {
      practiceCodesIndex = idx;
    }
  } else {
    // otherwise pot controls WPM as before
    int mapped = map(raw, 0, 4095, 5, 40);
    if (mapped != currentWPM) {
      currentWPM = mapped;
      updateDotMs();
    }
  }

  // buttons
  readButtons();
  uiHandleButtons();

  // display
  updateDisplay();

  // transmission
  handleTxState();
}
