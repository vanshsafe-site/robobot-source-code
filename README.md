# Robo Bot

A screen-only desk companion for an ESP32 and a 128×64 SH1106 OLED. No microphone, speaker, internet connection, or sensors are required. The bot's personality is animated locally; it does not listen or use AI.

## Hardware and upload

- OLED VCC → 3V3, GND → GND, SDA → GPIO21, SCL → GPIO22.
- Address: `0x3C`; display is rotated 180° as in the original sketch.
- Install **U8g2**, open `robobot.ino` in Arduino IDE, select your ESP32 board (ESP32 Dev Module for a typical WROOM), and upload.
- Serial Monitor: **115200 baud**, newline or both NL & CR.
- If your display needs a slower bus, change `setBusClock(400000)` to `setBusClock(100000)`.

## What's new

- Smooth eyes, natural blinking, glances, breathing motion, six expressions, and expressive mouths.
- Speech bubbles retain the face and wrap across two lines. Messages appear for a few seconds rather than replacing the face for up to 20 seconds.
- A 25-minute focus timer with pause/resume and a persistent visual completion message. It continues in other modes and brings the completion screen back when finished. There is no audible alarm.
- A repeating breathing animation: 4 seconds in, 2 seconds hold, 6 seconds out.
- Dim, slowly moving sleep graphics after a random 30–120 seconds without interaction in companion mode, unless a focus timer is running. Robo wakes automatically after 30 seconds of sleep. This is display dimming, not ESP32 deep sleep.
- Companion expressions, glances, blinking, and speech run automatically without Serial or button input. Focus, breathing, mood, and custom-message commands still need to be started explicitly.
- Nonblocking animation, debounced optional button, bounded serial input, and elapsed-time comparisons that tolerate `millis()` rollover.

## USB Serial controls

| Command | Action |
| --- | --- |
| `help` | Show commands |
| `tap` | Companion: cheerful reaction/message; focus: pause/resume (restart 25 minutes after completion); breathe: home; sleep: wake |
| `next` | Cycle companion → new 25-minute focus → breathe → sleep → companion |
| `home` | Return to companion; an active timer continues |
| `focus` / `focus 10` | Start a new 25-minute / 10-minute timer (1–180 minutes) |
| `pause` | Pause/resume while on the focus screen |
| `stop` | Cancel the timer and return home |
| `breathe` | Start guided breathing |
| `sleep` | Dim sleeping face; `tap` wakes it |
| `mood 0` … `mood 5` | Calm, happy, curious, surprised, sleepy, grumpy; hold the expression for 15 seconds |
| `say Hello, human!` | Show a custom message for 10 seconds (up to 48 printable ASCII characters) |
| `dim 80` | Set awake brightness (1–255) |
| `status` | Print mode, mood, uptime, and timer state; modes 0–3 = companion, focus, breathe, sleep |

Settings and timer state reset on reboot. Commands are lowercase. Long or non-ASCII input is rejected rather than partially executed.

## Optional single button

The sketch defaults to `BUTTON_PIN = -1`, so no button wiring is assumed. Set it to a suitable GPIO, such as **27**, and connect a momentary button between that pin and GND. The internal pull-up is enabled.

- **Tap:** the same action as the Serial `tap` command.
- **Hold for 800 ms:** cycle modes, once per hold.
- A button held during startup must be released before it can trigger an action.

Choose a pin suitable for your particular ESP32 board. GPIO34–39 on the classic ESP32 do not have internal pull-ups. If you deliberately use the onboard BOOT button (GPIO0 on many boards), only press it after startup; holding it during reset enters the bootloader.

Original project credit: [Tech Talkies](https://www.youtube.com/@techtalkies1).
