// Xbox 360 IR blaster over USB serial (115200).
// Wiring (KY-005 module): S -> pin 3, middle -> 5V, "-" -> GND.
// Commands, one per line:
//   p        power toggle      (RC6-6-32, command 0x0C)
//   e        eject / open-close (command 0x28)
//   c XX     any command byte, hex (e.g. "c 64" = Xbox guide button)
//   r HEX N  raw RC6 bits (mode + trailer + payload), N bits
// Xbox 360 remotes send RC6 mode 6 with 32 data bits 0x800F74cc; bit 15 is a toggle
// that must flip between distinct presses, so the sketch alternates it.
#include <Arduino.h>
#define IR_SEND_PIN 3
#include <IRremote.hpp>

static bool toggle;

static void sendCmdBase(uint32_t base, uint8_t cmd) {
  uint32_t payload = base | cmd | (toggle ? 0x8000UL : 0);  // Xbox 0x800F74xx, MCE 0x800F04xx
  toggle = !toggle;
  uint64_t raw = (0xCULL << 32) | payload;   // mode 110 + trailer 0, then 32 bits
  for (int i = 0; i < 3; i++) {              // the remote repeats while held; 3 frames is one press
    IrSender.sendRC6Raw(raw, 36);
    delay(40);
  }
  Serial.print(F("sent cmd 0x")); Serial.print(cmd, HEX);
  Serial.print(F(" payload 0x")); Serial.println(payload, HEX);
}

static void sendCmd(uint8_t cmd) { sendCmdBase(0x800F7400UL, cmd); }

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  IrSender.begin(IR_SEND_PIN);
  Serial.println(F("xbox_ir ready: p | e | c XX | r HEX N"));
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;
  digitalWrite(LED_BUILTIN, HIGH);
  char k = line[0];
  if (k == 'p') sendCmd(0x0C);
  else if (k == 'e') sendCmd(0x28);
  else if (k == 'c') {             // "c XX [T]": T forces the toggle bit (the host keeps it across port resets)
    char *end; uint8_t cmd = (uint8_t)strtoul(line.c_str() + 1, &end, 16);
    while (*end == ' ') end++;
    if (*end) toggle = (*end == '1');
    sendCmd(cmd);
  }
  else if (k == 'm') {             // "m XX [T]": Windows Media Center remote command (power-on reason 0x24)
    char *end; uint8_t cmd = (uint8_t)strtoul(line.c_str() + 1, &end, 16);
    while (*end == ' ') end++;
    if (*end) toggle = (*end == '1');
    sendCmdBase(0x800F0400UL, cmd);
  }
  else if (k == 'r') {
    char *end;
    uint64_t v = 0; const char *s = line.c_str() + 1;
    while (*s == ' ') s++;
    while (isxdigit(*s)) { v = (v << 4) | (uint8_t)(isdigit(*s) ? *s - '0' : (tolower(*s) - 'a' + 10)); s++; }
    uint8_t n = (uint8_t)strtoul(s, &end, 10);
    IrSender.sendRC6Raw(v, n ? n : 36);
    Serial.println(F("sent raw"));
  } else Serial.println(F("? p | e | c XX | r HEX N"));
  digitalWrite(LED_BUILTIN, LOW);
}
