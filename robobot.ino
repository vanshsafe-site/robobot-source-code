/*
  Robo Bot — a little OLED desk companion.
  Original Arduino project: Tech Talkies YouTube Channel.
  https://www.youtube.com/@techtalkies1

  ESP32 + SH1106 128x64: SDA 21, SCL 22, address 0x3C.
  U8g2 library required. Display retains the original 180-degree rotation.
  Serial Monitor: 115200 baud, newline. Send "help" for controls.
  Optional button to GND: set BUTTON_PIN below (-1 disables it).
*/
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_system.h>
#include <math.h>
#include <ctype.h>

constexpr int BUTTON_PIN = -1;
constexpr uint32_t FRAME_MS = 33;
constexpr uint32_t IDLE_SLEEP_MS = 5UL * 60UL * 1000UL;
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);

enum Mode { COMPANION, FOCUS, BREATHE, SLEEP };
enum Mood { CALM, HAPPY, CURIOUS, SURPRISED, SLEEPY, GRUMPY, MOOD_COUNT };
const char *const moodNames[] = {"CALM", "HAPPY", "CURIOUS", "WOW!", "SLEEPY", "GRUMPY"};
const char *const phrases[] = {
  "Hey, I'm Robo!", "One step at a time.", "Let's make something!",
  "Tiny bot. Big dreams.", "Time to stretch?", "You've got this!",
  "Thanks for hanging out.", "Stay curious.", "A little progress counts.",
  "Take a breath with me.", "Pixels with personality.", "Hello, human!"
};

struct Eye {
  float x, y, w, h;
  void ease(float nx, float ny, float nw, float nh) {
    x += (nx - x) * .24f; y += (ny - y) * .24f;
    w += (nw - w) * .24f; h += (nh - h) * .38f;
  }
  void draw() const {
    int iw = constrain((int)roundf(w), 2, 36);
    int ih = constrain((int)roundf(h), 2, 32);
    int r = max(0, min(iw, ih) / 2 - 1);
    u8g2.drawRBox(constrain((int)roundf(x - iw / 2.f), 0, 128 - iw),
                  constrain((int)roundf(y - ih / 2.f), 11, 46 - ih), iw, ih, r);
  }
};
Eye eyes[2] = {{40, 28, 25, 26}, {88, 28, 25, 26}};
Mode mode = COMPANION;
Mood mood = CALM;
uint32_t modeAt = 0, lastInput = 0, lastFrame = 0, lastAction = 0;
uint32_t actionWait = 2500, blinkAt = 0, blinkWait = 2200;
uint32_t messageAt = 0, messageDuration = 0, phraseAt = 0, phraseWait = 15000;
uint32_t focusAt = 0, focusDuration = 25UL * 60000UL;
bool blinking = false, focusRunning = false, focusDone = false;
float gazeX = 0, gazeY = 0;
uint8_t brightness = 160;
char message[49] = "Hello, human!";
char serialLine[80];
uint8_t serialLength = 0;
bool serialOverflow = false;
int lastPhrase = -1;

// Explicit declarations keep custom types compatible with Arduino preprocessing.
void setMode(Mode next, uint32_t now);
void say(const char *text, uint32_t now, uint32_t duration = 6500);
void command(char *line, uint32_t now);
void tap(uint32_t now);
void hold(uint32_t now);

void centered(const char *text, int baseline) {
  u8g2.drawStr(max(0, (128 - (int)u8g2.getStrWidth(text)) / 2), baseline, text);
}

void say(const char *text, uint32_t now, uint32_t duration) {
  snprintf(message, sizeof(message), "%s", text);
  messageAt = now;
  messageDuration = duration;
}

void setMode(Mode next, uint32_t now) {
  mode = next; modeAt = now; lastInput = now;
  gazeX = gazeY = 0;
  messageDuration = 0;
  mood = next == SLEEP ? SLEEPY : CALM;
  u8g2.setContrast(next == SLEEP ? 8 : brightness);
  if (next == COMPANION) {
    phraseAt = now;
    say("Good to see you!", now);
  }
}

void startFocus(uint32_t minutes, uint32_t now) {
  focusDuration = minutes * 60000UL;
  focusAt = now; focusRunning = true; focusDone = false;
  setMode(FOCUS, now);
}

void tap(uint32_t now) {
  lastInput = now;
  if (mode == SLEEP) { setMode(COMPANION, now); return; }
  if (mode == FOCUS) {
    if (focusDone) { startFocus(25, now); return; }
    if (focusRunning) {
      focusDuration -= min(focusDuration, (uint32_t)(now - focusAt));
      focusRunning = false;
    } else { focusAt = now; focusRunning = true; }
    return;
  }
  if (mode == BREATHE) { setMode(COMPANION, now); return; }
  mood = HAPPY;
  gazeX = gazeY = 0;
  lastAction = now; actionWait = 6000;
  int index;
  do { index = random(sizeof(phrases) / sizeof(phrases[0])); } while (index == lastPhrase);
  lastPhrase = index;
  say(phrases[index], now);
}

void hold(uint32_t now) {
  if (mode == COMPANION) startFocus(25, now);
  else if (mode == FOCUS) setMode(BREATHE, now);
  else if (mode == BREATHE) setMode(SLEEP, now);
  else setMode(COMPANION, now);
}

void readButton(uint32_t now) {
  if (BUTTON_PIN < 0) return;
  static bool raw = false, stable = false, armed = false, held = false;
  static uint32_t changed = 0, pressed = 0;
  bool reading = digitalRead(BUTTON_PIN) == LOW;
  if (reading != raw) { raw = reading; changed = now; }
  if (now - changed < 35) return;
  if (!raw && !stable) armed = true;
  if (raw != stable) {
    stable = raw;
    if (stable) { pressed = now; held = false; }
    else {
      if (armed && !held) tap(now);
      armed = true;
    }
  }
  if (stable && armed && !held && now - pressed >= 800) {
    held = true; hold(now);
  }
}

void command(char *line, uint32_t now) {
  if (!*line) return;
  if (!strcmp(line, "help")) {
    Serial.println("tap | next | home | sleep | breathe | focus [1..180] | pause | stop");
    Serial.println("mood 0..5 | say TEXT (48 ASCII chars) | dim 1..255 | status");
    return;
  }
  if (!strcmp(line, "status")) {
    Serial.printf("mode=%d mood=%s uptime=%lus focus=%s remaining=%lus\n", mode,
      moodNames[mood], (unsigned long)(now / 1000), focusRunning ? "running" : "paused",
      (unsigned long)((focusDuration - (focusRunning ? min(focusDuration, (uint32_t)(now - focusAt)) : 0)) / 1000));
    return;
  }
  if (!strcmp(line, "tap")) tap(now);
  else if (!strcmp(line, "next")) hold(now);
  else if (!strcmp(line, "home")) setMode(COMPANION, now);
  else if (!strcmp(line, "stop")) {
    focusRunning = focusDone = false; focusDuration = 0; setMode(COMPANION, now);
  }
  else if (!strcmp(line, "sleep")) setMode(SLEEP, now);
  else if (!strcmp(line, "breathe")) setMode(BREATHE, now);
  else if (!strcmp(line, "pause") && mode == FOCUS) tap(now);
  else if (!strncmp(line, "say ", 4) && line[4]) {
    if (strlen(line + 4) > 48) { Serial.println("Maximum message: 48 characters."); return; }
    setMode(COMPANION, now); say(line + 4, now, 10000);
  } else if (!strcmp(line, "focus")) startFocus(25, now);
  else {
    char *space = strchr(line, ' ');
    char *end = nullptr;
    long value = space ? strtol(space + 1, &end, 10) : -1;
    if (!space || end == space + 1 || *end) { Serial.println("Unknown command. Send help."); return; }
    if (!strncmp(line, "focus ", 6) && value >= 1 && value <= 180) startFocus(value, now);
    else if (!strncmp(line, "dim ", 4) && value >= 1 && value <= 255) {
      brightness = value; u8g2.setContrast(mode == SLEEP ? 8 : brightness);
    } else if (!strncmp(line, "mood ", 5) && value >= 0 && value < MOOD_COUNT) {
      setMode(COMPANION, now); mood = (Mood)value;
      messageDuration = 0; lastAction = now; actionWait = 15000;
    } else { Serial.println("Invalid command or value. Send help."); return; }
  }
  lastInput = now;
  Serial.println("OK");
}

void readSerial(uint32_t now) {
  // Bound each pass so a busy serial stream cannot starve the animation.
  for (int n = 0; n < 64 && Serial.available(); ++n) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (serialOverflow) Serial.println("Input too long or non-ASCII; command discarded.");
      else { serialLine[serialLength] = 0; command(serialLine, now); }
      serialLength = 0; serialOverflow = false;
    } else if (c == '\b' || c == 127) {
      if (serialLength) --serialLength;
    } else if ((unsigned char)c < 32 || (unsigned char)c > 126) serialOverflow = true;
    else if (serialLength < sizeof(serialLine) - 1) serialLine[serialLength++] = c;
    else serialOverflow = true;
  }
}

void update(uint32_t now) {
  if (focusRunning && now - focusAt >= focusDuration) {
    focusRunning = false; focusDone = true; focusDuration = 0;
    setMode(FOCUS, now); mood = HAPPY;
    Serial.println("Focus complete. Time for a break!");
  }
  if (mode == COMPANION && !focusRunning && now - lastInput >= IDLE_SLEEP_MS) setMode(SLEEP, now);
  if (mode == COMPANION && now - lastAction >= actionWait) {
    lastAction = now; actionWait = random(2500, 6000);
    gazeX = random(-7, 8); gazeY = random(-3, 4);
    // Mostly calm/cheerful; surprise and grumpiness are occasional accents.
    int choice = random(12);
    mood = choice < 6 ? CALM : choice < 9 ? HAPPY : choice == 9 ? CURIOUS : choice == 10 ? SURPRISED : GRUMPY;
  }
  if (mode == COMPANION && now - phraseAt >= phraseWait && now - messageAt >= messageDuration) {
    phraseAt = now; phraseWait = random(20000, 45000);
    int index;
    do { index = random(sizeof(phrases) / sizeof(phrases[0])); } while (index == lastPhrase);
    lastPhrase = index; say(phrases[index], now);
  }
  if (!blinking && now - blinkAt >= blinkWait) { blinking = true; blinkAt = now; }
  else if (blinking && now - blinkAt >= 130) { blinking = false; blinkAt = now; blinkWait = random(2200, 6000); }
}

void bubble() {
  // Two 24-character rows; wrap at spaces, or split a single long word.
  char first[25] = {}, second[25] = {};
  size_t length = strlen(message), split = min((size_t)24, length);
  if (length > 24) {
    for (size_t i = 24; i > 0; --i) {
      if (message[i] == ' ' && length - i - 1 <= 24) { split = i; break; }
    }
  }
  memcpy(first, message, split);
  const char *rest = message + split;
  if (*rest == ' ') ++rest;
  snprintf(second, sizeof(second), "%s", rest);
  u8g2.drawHLine(5, 46, 118);
  u8g2.drawLine(17, 46, 20, 43); u8g2.drawLine(20, 43, 23, 46);
  centered(first, *second ? 54 : 58);
  if (*second) centered(second, 63);
}

void face(uint32_t now, bool speaking) {
  float bob = sinf(now / 850.f) * 1.2f;
  for (int i = 0; i < 2; ++i) {
    float h = 26, w = 25, y = 28 + bob;
    if (mood == HAPPY) { h = 17; w = 28; }
    if (mood == CURIOUS) { h = i ? 29 : 20; w = i ? 27 : 22; }
    if (mood == SURPRISED) { h = 30; w = 20; }
    if (mood == SLEEPY) h = 4;
    if (mood == GRUMPY) h = 19;
    eyes[i].ease((i ? 88 : 40) + gazeX, y + gazeY, w, blinking ? 2 : h);
    eyes[i].draw();
    if (mood == GRUMPY && !blinking) {
      int x = (int)eyes[i].x, top = (int)(eyes[i].y - eyes[i].h / 2);
      u8g2.setDrawColor(0);
      if (!i) u8g2.drawTriangle(x - 14, top, x + 14, top, x + 14, top + 9);
      else u8g2.drawTriangle(x - 14, top, x + 14, top, x - 14, top + 9);
      u8g2.setDrawColor(1);
    }
  }
  if (speaking) { bubble(); return; }
  if (mood == SURPRISED) u8g2.drawCircle(64, 53, 4);
  else if (mood == HAPPY) {
    u8g2.drawLine(55, 50, 59, 54); u8g2.drawHLine(59, 54, 10); u8g2.drawLine(69, 54, 73, 50);
    u8g2.drawLine(21, 40, 24, 42); u8g2.drawLine(104, 42, 107, 40);
  } else u8g2.drawRBox(58, 51, 12, 3, 0);
  if (mode == FOCUS) {
    char time[24];
    uint32_t remaining = focusDuration - (focusRunning ? min(focusDuration, (uint32_t)(now - focusAt)) : 0);
    uint32_t seconds = (remaining + 999) / 1000;
    snprintf(time, sizeof(time), "%s %02lu:%02lu", focusRunning ? "FOCUS" : "PAUSED",
             (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    centered(time, 63);
  }
}

void render(uint32_t now) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_tf);
  if (mode == SLEEP) {
    // Slowly move the dim sleeping face to vary which OLED pixels are lit.
    int dx = (int)((now / 4000) % 9) - 4;
    int dy = (int)((now / 9000) % 5) - 2;
    u8g2.drawRBox(27 + dx, 30 + dy, 23, 3, 0);
    u8g2.drawRBox(78 + dx, 30 + dy, 23, 3, 0);
    u8g2.drawStr(104 + dx, 21 + dy, (now / 1100) % 2 ? "z" : "Z");
  } else if (mode == BREATHE) {
    uint32_t phase = (now - modeAt) % 12000;
    bool inhale = phase < 4000, exhale = phase >= 6000;
    float amount = inhale ? phase / 4000.f : exhale ? 1.f - (phase - 6000) / 6000.f : 1.f;
    int radius = 8 + (int)(amount * 12);
    centered("BREATHE WITH ROBO", 8);
    u8g2.drawCircle(64, 33, radius);
    u8g2.drawDisc(60, 31, 1); u8g2.drawDisc(68, 31, 1);
    u8g2.drawHLine(61, 37, 7);
    centered(inhale ? "Breathe in" : exhale ? "Breathe out" : "Hold gently", 63);
  } else {
    u8g2.drawStr(2, 8, "ROBO");
    const char *label = mode == FOCUS ? (focusDone ? "DONE!" : "FOCUS") : moodNames[mood];
    u8g2.drawStr(126 - u8g2.getStrWidth(label), 8, label);
    if (focusRunning && mode != FOCUS) u8g2.drawDisc(64, 4, 2);
    if (mode == FOCUS && focusDone) {
      say("Nice work! Take a break.", now, 1000);
      face(now, true);
    } else face(now, mode == COMPANION && now - messageAt < messageDuration);
  }
  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  if (BUTTON_PIN >= 0) pinMode(BUTTON_PIN, INPUT_PULLUP);
  Wire.begin(21, 22);
  u8g2.setI2CAddress(0x3C << 1);
  u8g2.setBusClock(400000);
  u8g2.begin(); u8g2.setContrast(brightness);
  randomSeed(esp_random());
  lastInput = modeAt = phraseAt = millis();
  say("Hey, I'm Robo!", millis());
  Serial.println("Robo ready. Send help for controls.");
}

void loop() {
  uint32_t now = millis();
  update(now);
  readSerial(now); readButton(now);
  uint32_t frameInterval = mode == SLEEP ? 200 : FRAME_MS;
  if (now - lastFrame >= frameInterval) { lastFrame = now; render(now); }
  delay(1);
}
