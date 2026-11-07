// Code by Grey and my buddy Claude AI
// CYD games (2.8" and 4"): menu with rainbow tic-tac-toe and a fishing game; rainbow spiral splash/celebrations.
// Pin map (ESP32-2432S028):
//   Display (HSPI)  MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, BL 21   -> set in platformio.ini for TFT_eSPI
//   Touch XPT2046   IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33         -> own pins, bit-banged (as in Fish Tank)
//   RGB LED         R 4, G 16, B 17 (active LOW)
//   LDR             GPIO 34 (analog)
//   Speaker         GPIO 26 (internal DAC2). Note GPIO25 (touch CLK) is DAC1.
//   SD card (VSPI)  CS 5, SCK 18, MISO 19, MOSI 23
// Freenove ESP32 Display 4.0" (env cyd40): ST7796 480x320, same display pins but BL 27, touch on the display bus
//   (TOUCH_CS 33), amp enable GPIO4 (active LOW). No LED/LDR pins known for it, so those are compiled out.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

#define LED_R 4
#define LED_G 16
#define LED_B 17
#define LDR_PIN 34

TFT_eSPI tft;

#ifdef BOARD_CYD40
// ---------------------------------------------------------------- 4" board: XPT2046 shares the display SPI bus
// (CLK 14, MOSI 13, MISO 12, CS 33), so TFT_eSPI drives it via TOUCH_CS. RGB LED / LDR pins unknown on this board.
#define HAS_LED_LDR 0
#define AMP_EN 4  // speaker amp enable, active LOW; held HIGH (off) here
static void xptBegin() {
  pinMode(AMP_EN, OUTPUT);
  digitalWrite(AMP_EN, HIGH);
}
static bool xptRead(uint16_t &xr, uint16_t &yr) {
  if (tft.getTouchRawZ() < 400) return false;
  uint32_t sx = 0, sy = 0;
  for (int k = 0; k < 3; k++) {
    uint16_t x, y;
    tft.getTouchRaw(&x, &y);
    sx += x;
    sy += y;
  }
  xr = sx / 3;
  yr = sy / 3;
  return true;
}
#else
#define HAS_LED_LDR 1
// ---------------------------------------------------------------- XPT2046 (bit-banged, from Fish Tank hal_cyd28.cpp)
static void xptWrite(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(XPT2046_MOSI, cmd & (1 << i));
    digitalWrite(XPT2046_CLK, LOW);
    delayMicroseconds(1);
    digitalWrite(XPT2046_CLK, HIGH);
    delayMicroseconds(1);
  }
  digitalWrite(XPT2046_MOSI, LOW);
  digitalWrite(XPT2046_CLK, LOW);
}

static uint16_t xptConvert(uint8_t cmd) {
  xptWrite(cmd);
  uint16_t v = 0;
  for (int i = 11; i >= 0; i--) {
    digitalWrite(XPT2046_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(XPT2046_CLK, LOW);
    delayMicroseconds(1);
    v |= digitalRead(XPT2046_MISO) << i;
  }
  return v;
}

static void xptBegin() {
  pinMode(XPT2046_IRQ, INPUT);
  pinMode(XPT2046_MOSI, OUTPUT);
  pinMode(XPT2046_MISO, INPUT);
  pinMode(XPT2046_CLK, OUTPUT);
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);
  digitalWrite(XPT2046_CLK, LOW);
}

// Raw 12-bit readings; true while pressed.
static bool xptRead(uint16_t &xr, uint16_t &yr) {
  digitalWrite(XPT2046_CS, LOW);
  int z = xptConvert(0xB1) + 4095 - xptConvert(0xC1);
  if (z < 400) {
    digitalWrite(XPT2046_CS, HIGH);
    return false;
  }
  xptConvert(0x91);  // first conversion after a channel change is unreliable
  uint32_t sx = 0, sy = 0;
  for (int k = 0; k < 3; k++) {
    sy += xptConvert(0xD1);
    sx += xptConvert(0x91);
  }
  xptConvert(0xD0);  // power down
  digitalWrite(XPT2046_CS, HIGH);
  xr = sx / 3;
  yr = sy / 3;
  return true;
}
#endif

// ---------------------------------------------------------------- 4-corner calibration (saved in NVS)
// Screen x = cx0 + cxk * raw[swap ? 1 : 0], screen y = cy0 + cyk * raw[swap ? 0 : 1]. Handles mirrored and
// swapped axes, so the same code works on every CYD panel/rotation. Hold the top bar ~2 s to redo it.
struct TouchCal {
  float cx0, cxk, cy0, cyk;
  uint8_t swap;
};
static TouchCal cal;
static const int CAL_M = 30;  // crosshair inset from the edges

static bool loadCal() {
  Preferences p;
  p.begin("touch", true);
  size_t n = p.getBytes("cal4", &cal, sizeof(cal));
  p.end();
  return n == sizeof(cal) && cal.cxk != 0 && cal.cyk != 0;
}

static void saveCal() {
  Preferences p;
  p.begin("touch", false);
  p.putBytes("cal4", &cal, sizeof(cal));
  p.end();
}

static void crosshair(int x, int y, uint16_t c) {
  tft.drawFastHLine(x - 12, y, 25, c);
  tft.drawFastVLine(x, y - 12, 25, c);
  tft.drawCircle(x, y, 6, c);
}

// Waits for a press, averages it until release, returns the mean raw reading.
static void readHeldPoint(float &a, float &b) {
  uint16_t xr, yr;
  for (;;) {
    while (xptRead(xr, yr)) delay(10);  // release the previous touch first
    delay(80);
    while (!xptRead(xr, yr)) delay(10);
    delay(60);                           // skip the first, bouncy samples
    uint32_t sa = 0, sb = 0, n = 0;
    while (xptRead(xr, yr)) {
      sa += xr; sb += yr; n++;
      delay(10);
    }
    if (n >= 5) { a = float(sa) / n; b = float(sb) / n; return; }
  }
}

static void calibrate() {
  const int W = tft.width(), H = tft.height();
  const int px[4] = {CAL_M, W - 1 - CAL_M, W - 1 - CAL_M, CAL_M};
  const int py[4] = {CAL_M, CAL_M, H - 1 - CAL_M, H - 1 - CAL_M};
  float ra[4], rb[4];
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Touch calibration", W / 2, H / 2 - 24, 4);
  tft.drawCentreString("Press and release each red crosshair", W / 2, H / 2 + 8, 2);
  for (int i = 0; i < 4; i++) {
    crosshair(px[i], py[i], TFT_RED);
    readHeldPoint(ra[i], rb[i]);
    crosshair(px[i], py[i], TFT_DARKGREY);
    Serial.printf("cal %d at %d,%d raw %.0f,%.0f\n", i, px[i], py[i], ra[i], rb[i]);
  }
  // Which raw channel moves when going left -> right along the top/bottom edges?
  const float da = fabsf(ra[1] - ra[0]) + fabsf(ra[2] - ra[3]);
  const float db = fabsf(rb[1] - rb[0]) + fabsf(rb[2] - rb[3]);
  cal.swap = db > da;
  const float *rx = cal.swap ? rb : ra, *ry = cal.swap ? ra : rb;
  const float xl = (rx[0] + rx[3]) / 2, xr = (rx[1] + rx[2]) / 2;
  const float yt = (ry[0] + ry[1]) / 2, yb = (ry[2] + ry[3]) / 2;
  cal.cxk = (W - 1 - 2 * CAL_M) / (xr - xl);
  cal.cx0 = CAL_M - xl * cal.cxk;
  cal.cyk = (H - 1 - 2 * CAL_M) / (yb - yt);
  cal.cy0 = CAL_M - yt * cal.cyk;
  saveCal();
  Serial.printf("cal saved: swap=%d x=%.2f+%.4f*r y=%.2f+%.4f*r\n", cal.swap, cal.cx0, cal.cxk, cal.cy0, cal.cyk);
}

static bool touchPoint(int &x, int &y, uint16_t &xr, uint16_t &yr) {
  if (!xptRead(xr, yr)) return false;
  const float rx = cal.swap ? yr : xr, ry = cal.swap ? xr : yr;
  x = constrain(lroundf(cal.cx0 + cal.cxk * rx), 0, tft.width() - 1);
  y = constrain(lroundf(cal.cy0 + cal.cyk * ry), 0, tft.height() - 1);
  return true;
}

// ---------------------------------------------------------------- RGB LED (active LOW)
static void led(bool r, bool g, bool b) {
#if HAS_LED_LDR
  digitalWrite(LED_R, !r);
  digitalWrite(LED_G, !g);
  digitalWrite(LED_B, !b);
#endif
}


// ---------------------------------------------------------------- rainbow spiral
// Every pixel gets a fixed "phase" byte (angle * arms + radius * twist) once at start; each frame looks it up in a
// 256-entry palette that sweeps the whole hue wheel, with saturation and brightness drifting over time, so across a
// few seconds the 16-bit panel shows a huge range of its 65,536 colours. Rotating the palette makes it spin.
static const int SPIRAL_ARMS = 3;
static const float SPIRAL_TWIST = 2.2f;  // phase steps per pixel of radius
static const int STRIP_ROWS = 16;
static uint8_t **spiralPhase = nullptr;  // one malloc per row: the ESP32 heap has no single 150 KB block
static uint16_t *stripBuf = nullptr;

static uint16_t hsv565(float h, float s, float v) {  // h 0..1
  const float hh = h * 6.0f;
  const int i = int(hh) % 6;
  const float f = hh - floorf(hh);
  const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  float r, g, b;
  switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return tft.color565(uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255));
}

static bool spiralBegin() {
  const int W = tft.width(), H = tft.height();
  stripBuf = (uint16_t *)malloc(W * STRIP_ROWS * sizeof(uint16_t));
  spiralPhase = (uint8_t **)calloc(H, sizeof(uint8_t *));
  if (!stripBuf || !spiralPhase) {
    Serial.println("spiral: out of memory");
    return false;
  }
  const float cx = W / 2.0f, cy = H / 2.0f;
  for (int y = 0; y < H; y++) {
    spiralPhase[y] = (uint8_t *)malloc(W);
    if (!spiralPhase[y]) {
      Serial.printf("spiral: out of memory at row %d\n", y);
      return false;
    }
    for (int x = 0; x < W; x++) {
      const float dx = x - cx, dy = y - cy;
      const float a = atan2f(dy, dx) / (2 * PI);  // -0.5..0.5
      const float r = sqrtf(dx * dx + dy * dy);
      spiralPhase[y][x] =uint8_t(int(a * 256 * SPIRAL_ARMS + r * SPIRAL_TWIST) & 255);
    }
  }
  return true;
}

static void spiralFrame() {
  static uint16_t pal[256];
  static uint32_t frame = 0;
  const float t = frame * 0.02f;
  const float sat = 0.75f + 0.25f * sinf(t * 0.7f);   // pastel <-> vivid
  const float val = 0.70f + 0.30f * sinf(t * 1.1f);   // dim <-> bright
  for (int i = 0; i < 256; i++) {
    // Brightness also ripples along the arm so each frame has light and dark versions of every hue.
    const float v = val * (0.55f + 0.45f * sinf(i * (2 * PI / 64) + t * 3));
    pal[i] = hsv565(fmodf(i / 256.0f + t * 0.05f, 1.0f), sat, v);
  }
  const uint8_t shift = uint8_t(frame * 3);  // spin
  const int W = tft.width(), H = tft.height();
  tft.startWrite();
  for (int y0 = 0; y0 < H; y0 += STRIP_ROWS) {
    const int rows = min(STRIP_ROWS, H - y0);
    uint16_t *dst = stripBuf;
    for (int r = 0; r < rows; r++) {
      const uint8_t *src = spiralPhase[y0 + r];
      for (int x = 0; x < W; x++) *dst++ = pal[uint8_t(src[x] - shift)];
    }
    tft.pushImage(0, y0, W, rows, stripBuf);
  }
  tft.endWrite();
  frame++;
}

// ---------------------------------------------------------------- rainbow tic-tac-toe
// You are X. Opponent: CPU (wins/blocks most of the time, so it is beatable) or a second player. Grid and marks
// are drawn in the spiral's hues and keep cycling; a win plays the full spinning spiral, then a new game starts.
enum { EMPTY = 0, PX = 1, PO = 2, DRAW = 3 };
static const uint8_t LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
static uint8_t board[9];
static uint8_t turn = PX, starter = PX, winner = EMPTY;
static int winLine = -1;
static bool vsCpu = true;
static int scoreX = 0, scoreO = 0, scoreD = 0;
static uint32_t cpuAt = 0, resultAt = 0;

// Layout (works for 480x320 and 320x240)
static int BX, BY, BS, CELL, PNX, PNW, BTN_H, FONT;
static const int NBTN = 4;
static void goMenu();
static int btnY[NBTN];

static float hueNow() { return fmodf(millis() / 6000.0f, 1.0f); }  // slow global hue drift
static uint16_t rainbow(float offset) { return hsv565(fmodf(hueNow() + offset + 1.0f, 1.0f), 1.0f, 1.0f); }

static void layout() {
  const int W = tft.width(), H = tft.height();
  BS = min(H, W) - 20;
  BS -= BS % 3;
  CELL = BS / 3;
  BX = 10;
  BY = (H - BS) / 2;
  PNX = BX + BS + 12;
  PNW = W - PNX - 8;
  FONT = W >= 480 ? 4 : 2;
  BTN_H = W >= 480 ? 40 : 30;
  for (int i = 0; i < NBTN; i++) btnY[i] = H - (NBTN - i) * (BTN_H + 8);
}

static void drawButton(int i, const char *label) {
  const uint16_t c = rainbow(0.12f * i + 0.5f);
  tft.drawRoundRect(PNX, btnY[i], PNW, BTN_H, 8, c);
  tft.drawRoundRect(PNX + 1, btnY[i] + 1, PNW - 2, BTN_H - 2, 7, c);
  tft.setTextColor(c, TFT_BLACK);
  tft.setTextPadding(PNW - 8);
  tft.drawCentreString(label, PNX + PNW / 2, btnY[i] + (BTN_H - tft.fontHeight(FONT)) / 2, FONT);
  tft.setTextPadding(0);
}

static void drawPanel() {
  const bool big = tft.width() >= 480;
  tft.setTextPadding(PNW);
  tft.setTextColor(rainbow(0.0f), TFT_BLACK);
  tft.drawString(big ? "Tic Tac Toe" : "TicTac", PNX, BY, FONT);

  char buf[24];
  const char *status;
  if (winner == PX) status = vsCpu ? "You win!" : "X wins!";
  else if (winner == PO) status = vsCpu ? "CPU wins" : "O wins!";
  else if (winner == DRAW) status = "Draw";
  else if (turn == PX) status = vsCpu ? (big ? "Your turn" : "You") : "X to play";
  else status = vsCpu ? (big ? "CPU thinking" : "CPU...") : "O to play";
  tft.setTextColor(winner ? TFT_WHITE : rainbow(turn == PX ? 0.0f : 0.5f), TFT_BLACK);
  tft.drawString(status, PNX, BY + tft.fontHeight(FONT) + 10, FONT);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(buf, sizeof(buf), big ? "X %d  O %d  = %d" : "%d-%d-%d", scoreX, scoreO, scoreD);
  tft.drawString(buf, PNX, BY + 2 * tft.fontHeight(FONT) + 20, 2);
  tft.setTextPadding(0);

  drawButton(0, big ? "New game" : "New");
  drawButton(1, vsCpu ? (big ? "vs CPU" : "CPU") : (big ? "2 players" : "2P"));
  drawButton(2, big ? "Calibrate" : "Cal");
  drawButton(3, "Menu");
}

static void cellCentre(int i, int &cx, int &cy) {
  cx = BX + (i % 3) * CELL + CELL / 2;
  cy = BY + (i / 3) * CELL + CELL / 2;
}

static void drawX(int i, uint16_t c) {
  int cx, cy;
  cellCentre(i, cx, cy);
  const int r = CELL * 3 / 10, w = max(3, CELL / 18);
  for (int o = -w; o <= w; o++) {
    tft.drawLine(cx - r + o, cy - r, cx + r + o, cy + r, c);
    tft.drawLine(cx - r + o, cy + r, cx + r + o, cy - r, c);
  }
}

static void drawO(int i, uint16_t c) {
  int cx, cy;
  cellCentre(i, cx, cy);
  const int r = CELL * 3 / 10, w = max(3, CELL / 16);
  for (int k = 0; k < w; k++) tft.drawCircle(cx, cy, r - k, c);
  for (int k = 0; k < w - 1; k++) tft.drawCircle(cx + 1, cy, r - k, c);  // fill the gaps between rings
}

// Redrawn every ~50 ms: the colours keep moving, nothing is erased so there is no flicker.
static void drawBoardColours() {
  const int lw = max(4, CELL / 20);
  for (int k = 1; k < 3; k++) {
    tft.fillRect(BX + k * CELL - lw / 2, BY, lw, BS, rainbow(0.15f * k));
    tft.fillRect(BX, BY + k * CELL - lw / 2, BS, lw, rainbow(0.15f * k + 0.3f));
  }
  for (int i = 0; i < 9; i++) {
    if (board[i] == PX) drawX(i, rainbow(0.0f + i * 0.03f));
    else if (board[i] == PO) drawO(i, rainbow(0.5f + i * 0.03f));
  }
  if (winLine >= 0) {
    int ax, ay, bx, by;
    cellCentre(LINES[winLine][0], ax, ay);
    cellCentre(LINES[winLine][2], bx, by);
    const uint16_t c = (millis() / 150) % 2 ? TFT_WHITE : rainbow(0.25f);
    for (int o = -3; o <= 3; o++) {
      tft.drawLine(ax + o, ay, bx + o, by, c);
      tft.drawLine(ax, ay + o, bx, by + o, c);
    }
  }
}

static void redrawAll() {
  tft.fillScreen(TFT_BLACK);
  drawBoardColours();
  drawPanel();
}

static void newGame() {
  memset(board, 0, sizeof(board));
  winner = EMPTY;
  winLine = -1;
  turn = starter;
  starter = starter == PX ? PO : PX;  // take turns going first
  cpuAt = (vsCpu && turn == PO) ? millis() + 600 : 0;
  redrawAll();
}

static uint8_t checkWinner(const uint8_t *b, int *line = nullptr) {
  for (int l = 0; l < 8; l++) {
    const uint8_t v = b[LINES[l][0]];
    if (v && v == b[LINES[l][1]] && v == b[LINES[l][2]]) {
      if (line) *line = l;
      return v;
    }
  }
  for (int i = 0; i < 9; i++)
    if (!b[i]) return EMPTY;
  return DRAW;
}

// Cell that completes a line for player p, or -1.
static int findFinisher(uint8_t p) {
  for (int i = 0; i < 9; i++) {
    if (board[i]) continue;
    board[i] = p;
    const bool w = checkWinner(board) == p;
    board[i] = EMPTY;
    if (w) return i;
  }
  return -1;
}

static int cpuChoose() {
  const bool smart = random(100) < 85;  // 15% of the time it plays loosely, so you can win
  int m = smart ? findFinisher(PO) : -1;
  if (m < 0 && smart) m = findFinisher(PX);
  if (m < 0 && !board[4] && random(100) < 70) m = 4;
  if (m < 0) {
    const int corners[4] = {0, 2, 6, 8};
    int freeCells[9], n = 0;
    for (int c : corners)
      if (!board[c]) freeCells[n++] = c;
    if (!n || random(100) < 30) {
      n = 0;
      for (int i = 0; i < 9; i++)
        if (!board[i]) freeCells[n++] = i;
    }
    m = freeCells[random(n)];
  }
  return m;
}

static void play(int i) {
  if (board[i] || winner) return;
  board[i] = turn;
  winner = checkWinner(board, &winLine);
  if (winner) {
    if (winner == PX) scoreX++;
    else if (winner == PO) scoreO++;
    else scoreD++;
    resultAt = millis();
    Serial.printf("game over: %s\n", winner == PX ? "X" : winner == PO ? "O" : "draw");
  } else {
    turn = turn == PX ? PO : PX;
    if (vsCpu && turn == PO) cpuAt = millis() + 500;
  }
  drawBoardColours();
  drawPanel();
}

static void celebrate(uint32_t ms) {
  if (!spiralPhase) return;
  const uint32_t end = millis() + ms;
  uint16_t xr, yr;
  while (millis() < end) {
    spiralFrame();
    if (xptRead(xr, yr)) break;  // tap to skip
  }
  while (xptRead(xr, yr)) delay(10);
}

static void handleTap(int x, int y) {
  if (x >= BX && x < BX + BS && y >= BY && y < BY + BS) {
    if (winner) return;
    if (vsCpu && turn == PO) return;  // CPU is moving
    play((y - BY) / CELL * 3 + (x - BX) / CELL);
    return;
  }
  if (x < PNX) return;
  for (int b = 0; b < NBTN; b++) {
    if (y < btnY[b] || y >= btnY[b] + BTN_H) continue;
    if (b == 0) {
      newGame();
    } else if (b == 1) {
      vsCpu = !vsCpu;
      scoreX = scoreO = scoreD = 0;
      starter = PX;
      newGame();
    } else if (b == 2) {
      calibrate();
      redrawAll();
    } else {
      goMenu();
    }
    return;
  }
}

// ---------------------------------------------------------------- fishing game
// Tap the water: the boat moves there and drops the hook to that depth. A fish that swims into the hook bites.
// Then HOLD to reel in. Holding raises line tension, letting go eases it; at 100% the line snaps. Bigger fish
// pull harder, the rainbow fish is fast and worth the most, and jellyfish sting (line cut, -5 s).
// 90 s rounds, best score kept in NVS. The scene is drawn into a 32-row sprite strip by strip (no PSRAM).
TFT_eSprite spr(&tft);
static const int FSTRIP = 32;
static int oy = 0;  // y of the strip being drawn
static inline int Y(float y) { return int(y) - oy; }
static inline bool inStrip(float top, float bot) { return bot >= oy && top < oy + FSTRIP; }

enum FishType { F_SMALL, F_MED, F_BIG, F_GOLD, F_JELLY, F_NTYPES };
struct FishDef {
  float len, speed, pull;
  int pts;
  float hue;
};
static const FishDef FDEF[F_NTYPES] = {
    {22, 1.3f, 0.5f, 10, 0.08f},   // small orange
    {34, 0.9f, 0.9f, 25, 0.50f},   // medium teal
    {52, 0.6f, 1.5f, 60, 0.83f},   // big magenta
    {26, 2.4f, 1.1f, 150, 0.0f},   // rainbow (colour cycles)
    {28, 0.4f, 0.0f, 0, 0.75f},    // jellyfish
};
struct Fish {
  float x, y, vx, phase;
  uint8_t type;
};
static const int NFISH = 8;
static Fish fish[NFISH];

enum HookState { H_IDLE, H_MOVE, H_DROP, H_WAIT, H_REEL, H_HOOKED };
struct Bubble { float x, y; };
static Bubble bubbles[6];
static struct {
  float S;
  int surf, sand;
  float boatX, boatTarget;
  float hookX, hookY, targetY;
  HookState hs;
  int hooked;
  float tension;
  uint32_t stateAt, roundEnd;
  int score, best, caught;
  bool over, newBest;
  int popPts;
  float popX;
  uint32_t popAt;
  const char *msg;
  uint32_t msgAt;
} fg;
static uint16_t rowCol[480];
static bool fishReady = false;

static const int ROUND_MS = 90000;

static uint16_t mix565(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1, float t) {
  return tft.color565(r0 + (r1 - r0) * t, g0 + (g1 - g0) * t, b0 + (b1 - b0) * t);
}

static float rodTipX() { return fg.boatX + 46 * fg.S; }
static float rodTipY() { return fg.surf - 44 * fg.S; }

static void spawnFish(Fish &f, bool anywhere) {
  const int W = tft.width();
  const float S = fg.S;
  const float y = random(int(fg.surf + 28 * S), int(fg.sand - 16 * S));
  const float d = (y - fg.surf) / float(fg.sand - fg.surf);  // 0 shallow .. 1 deep
  const float w[F_NTYPES] = {1.2f * (1 - d) + 0.2f, 0.7f, 1.3f * d, 0.10f, 0.14f};
  float tot = 0;
  for (float v : w) tot += v;
  float r = random(10000) / 10000.0f * tot;
  uint8_t t = 0;
  while (t < F_NTYPES - 1 && r >= w[t]) r -= w[t++];
  const int dir = random(2) ? 1 : -1;
  f.type = t;
  f.y = y;
  f.vx = dir * FDEF[t].speed * S * (0.8f + random(40) / 100.0f);
  f.x = anywhere ? random(W) : (dir > 0 ? -60 * S : W + 60 * S);
  f.phase = random(628) / 100.0f;
}

static void fishNewRound() {
  fg.score = 0;
  fg.caught = 0;
  fg.over = false;
  fg.newBest = false;
  fg.hs = H_IDLE;
  fg.hooked = -1;
  fg.tension = 0;
  fg.boatX = fg.boatTarget = tft.width() * 0.35f;
  fg.roundEnd = millis() + ROUND_MS;
  fg.msg = "Tap the water to cast";
  fg.msgAt = millis();
  fg.popAt = 0;
  for (auto &f : fish) spawnFish(f, true);
}

static bool fishBegin() {
  const int W = tft.width(), H = tft.height();
  spr.setColorDepth(16);
  if (!spr.createSprite(W, FSTRIP)) {
    Serial.println("fishing: no memory for sprite");
    return false;
  }
  fg.S = W / 480.0f;
  fg.surf = H * 0.24f;
  fg.sand = H - int(18 * fg.S);
  for (int y = 0; y < H && y < 480; y++) {
    if (y < fg.surf) rowCol[y] = mix565(90, 170, 250, 200, 232, 255, float(y) / fg.surf);
    else if (y < fg.sand) rowCol[y] = mix565(0, 160, 200, 0, 25, 80, float(y - fg.surf) / (fg.sand - fg.surf));
    else rowCol[y] = mix565(214, 190, 120, 170, 140, 80, float(y - fg.sand) / (H - fg.sand));
  }
  for (auto &b : bubbles) b = {float(random(W)), float(random(fg.surf, fg.sand))};
  Preferences p;
  p.begin("fishing", true);
  fg.best = p.getInt("best", 0);
  p.end();
  fishNewRound();
  return true;
}

static void hookFree(const char *why) {
  fg.msg = why;
  fg.msgAt = millis();
  if (fg.hooked >= 0) {
    Fish &f = fish[fg.hooked];
    f.vx = (random(2) ? 1 : -1) * FDEF[f.type].speed * fg.S * 2.0f;  // dart away
  }
  fg.hooked = -1;
  fg.tension = 0;
  fg.hs = H_REEL;
}

static void endRound() {
  fg.over = true;
  if (fg.hooked >= 0) fg.hooked = -1;
  fg.hs = H_IDLE;
  if (fg.score > fg.best) {
    fg.best = fg.score;
    fg.newBest = true;
    Preferences p;
    p.begin("fishing", false);
    p.putInt("best", fg.best);
    p.end();
  }
  Serial.printf("fishing round over: score %d, caught %d, best %d\n", fg.score, fg.caught, fg.best);
}

static void celebrate(uint32_t ms);

static void fishUpdate(float dt, bool down, bool tap, int tx, int ty) {
  const uint32_t now = millis();
  const int W = tft.width();
  const float S = fg.S;

  for (int i = 0; i < NFISH; i++) {
    Fish &f = fish[i];
    if (fg.hs == H_HOOKED && i == fg.hooked) continue;
    f.phase += 0.15f * dt;
    f.x += f.vx * dt;
    f.y += sinf(f.phase * 0.3f) * 0.25f * dt * S;
    if (f.type == F_JELLY) f.y += sinf(f.phase * 0.5f) * 0.6f * dt * S;
    f.y = constrain(f.y, fg.surf + 20 * S, fg.sand - 10 * S);
    if (f.x < -90 * S || f.x > W + 90 * S) spawnFish(f, false);
  }
  for (auto &b : bubbles) {
    b.y -= 0.8f * S * dt;
    b.x += sinf(b.y * 0.05f) * 0.3f;
    if (b.y < fg.surf) b = {float(random(W)), float(fg.sand - 4)};
  }

  if (fg.over) {
    if (tap) fishNewRound();
    return;
  }
  if (now >= fg.roundEnd) {
    endRound();
    if (fg.newBest) celebrate(3000);
    return;
  }

  const float idleY = fg.surf + 6 * S;
  switch (fg.hs) {
    case H_IDLE:
      fg.hookX = rodTipX();
      fg.hookY = idleY;
      if (tap && ty > fg.surf) {
        fg.boatTarget = constrain(tx - 46 * S, 40 * S, W - 60 * S);
        fg.targetY = constrain(ty, fg.surf + 24 * S, fg.sand - 6 * S);
        fg.hs = H_MOVE;
      }
      break;
    case H_MOVE: {
      const float d = fg.boatTarget - fg.boatX, step = 4 * S * dt;
      if (fabsf(d) <= step) {
        fg.boatX = fg.boatTarget;
        fg.hs = H_DROP;
      } else {
        fg.boatX += d > 0 ? step : -step;
      }
      fg.hookX = rodTipX();
      fg.hookY = idleY;
      break;
    }
    case H_DROP:
      fg.hookX = rodTipX();
      fg.hookY += 3.5f * S * dt;
      if (fg.hookY >= fg.targetY) {
        fg.hookY = fg.targetY;
        fg.hs = H_WAIT;
        fg.stateAt = now;
      }
      if (tap) fg.hs = H_REEL;
      break;
    case H_WAIT:
      fg.hookY = fg.targetY + sinf(now / 300.0f) * 2 * S;
      if (now - fg.stateAt > 3500 || tap) fg.hs = H_REEL;
      break;
    case H_REEL:
      fg.hookY -= 5 * S * dt;
      fg.hookX += (rodTipX() - fg.hookX) * 0.1f * dt;
      if (fg.hookY <= idleY) fg.hs = H_IDLE;
      break;
    case H_HOOKED: {
      Fish &f = fish[fg.hooked];
      const FishDef &d = FDEF[f.type];
      float pull = d.pull * S * (0.7f + 0.5f * sinf(now / 260.0f + f.phase));
      if (random(1000) < 15) pull *= 2.5f;  // sudden surge
      fg.hookY += pull * dt;
      if (down) {
        fg.hookY -= 3.2f * S * dt;
        fg.tension += (d.pull * 1.1f + 0.4f) * dt;
      } else {
        fg.tension -= 2.5f * dt;
      }
      fg.tension = constrain(fg.tension, 0.0f, 100.0f);
      f.phase += 0.4f * dt;
      f.x += sinf(now / 200.0f + f.phase) * 1.5f * S * dt;
      f.x = constrain(f.x, 20 * S, W - 20 * S);
      fg.hookX = f.x;
      fg.hookY = min(fg.hookY, float(fg.sand - 4 * S));
      f.y = fg.hookY;
      if (fg.tension >= 100) {
        hookFree("Line snapped!");
      } else if (fg.hookY <= fg.surf + 4 * S) {
        fg.score += d.pts;
        fg.caught++;
        fg.popPts = d.pts;
        fg.popX = f.x;
        fg.popAt = now;
        Serial.printf("caught type %d (+%d) score %d\n", f.type, d.pts, fg.score);
        spawnFish(f, false);
        fg.hooked = -1;
        fg.tension = 0;
        fg.hs = H_IDLE;
      }
      break;
    }
  }

  // Bites: anything near the hook while it is in the water and free.
  if (fg.hs == H_DROP || fg.hs == H_WAIT || fg.hs == H_REEL) {
    for (int i = 0; i < NFISH; i++) {
      Fish &f = fish[i];
      const float L = FDEF[f.type].len * S;
      if (fabsf(f.x - fg.hookX) < L * 0.5f && fabsf(f.y - fg.hookY) < max(L * 0.3f, 8 * S)) {
        if (f.type == F_JELLY) {
          fg.roundEnd -= 5000;
          hookFree("Jellyfish! -5 s");
        } else {
          fg.hooked = i;
          fg.hs = H_HOOKED;
          fg.tension = 15;
          fg.msg = "Fish on! HOLD to reel";
          fg.msgAt = now;
        }
        break;
      }
    }
  }
}

static void drawFish(const Fish &f) {
  const FishDef &d = FDEF[f.type];
  const float S = fg.S, L = d.len * S;
  if (!inStrip(f.y - L, f.y + L)) return;
  const int x = f.x, y = Y(f.y);
  if (f.type == F_JELLY) {
    const uint16_t c = hsv565(fmodf(d.hue + 0.08f * sinf(f.phase), 1.0f), 0.45f, 1.0f);
    const int r = L / 2;
    spr.fillEllipse(x, y, r, r * 0.7f, c);
    for (int k = -2; k <= 2; k++) {
      const int bx = x + k * r / 2.5f;
      spr.drawLine(bx, y + r * 0.5f, bx + sinf(f.phase + k) * 4 * S, y + r * 1.7f, c);
    }
    spr.fillCircle(x - r / 3, y - r / 5, max(1, r / 6), TFT_WHITE);
    return;
  }
  const bool rainbowFish = f.type == F_GOLD;
  const uint16_t body = rainbowFish ? rainbow(f.x / 300.0f) : hsv565(d.hue, 0.85f, 1.0f);
  const uint16_t fin = rainbowFish ? rainbow(f.x / 300.0f + 0.5f) : hsv565(d.hue, 1.0f, 0.6f);
  const int dir = f.vx >= 0 ? 1 : -1;
  const int rx = L / 2, ry = L / 4.5f + 1;
  const int wig = sinf(f.phase * 2) * ry * 0.5f;
  const int tx = x - dir * rx;
  spr.fillTriangle(tx + dir * 3, y, tx - dir * rx * 0.6f, y - ry - wig, tx - dir * rx * 0.6f, y + ry - wig, fin);
  spr.fillEllipse(x, y, rx, ry, body);
  spr.fillTriangle(x - dir * rx * 0.3f, y - ry + 1, x + dir * rx * 0.2f, y - ry + 1, x - dir * rx * 0.5f, y - ry * 1.8f, fin);
  if (rainbowFish) spr.drawFastHLine(x - rx / 2, y, rx, TFT_WHITE);
  const int ex = x + dir * rx * 0.55f, ey = y - ry * 0.25f;
  spr.fillCircle(ex, ey, max(1, int(ry * 0.32f)), TFT_WHITE);
  spr.fillCircle(ex + dir, ey, max(1, int(ry * 0.16f)), TFT_BLACK);
}

static void fishRender() {
  const int W = tft.width(), H = tft.height();
  const float S = fg.S, t = millis() / 1000.0f;
  const bool big = W >= 480;
  const int font = big ? 4 : 2;
  const uint32_t now = millis();
  const int timeLeft = fg.over ? 0 : max(0, int((fg.roundEnd - now + 999) / 1000));
  char buf[48];

  for (oy = 0; oy < H; oy += FSTRIP) {
    const int rows = min(FSTRIP, H - oy);
    for (int r = 0; r < rows; r++) spr.drawFastHLine(0, r, W, rowCol[oy + r]);

    // sun
    const float sx = W * 0.6f, sy = 26 * S, sr = 15 * S;
    if (inStrip(sy - sr, sy + sr)) spr.fillCircle(sx, Y(sy), sr, TFT_YELLOW);

    // waves
    const int bandTop = fg.surf - 5 * S, bandBot = fg.surf + 5 * S;
    if (inStrip(bandTop, bandBot)) {
      for (int x = 0; x < W; x += 3) {
        const int wy = fg.surf + 3 * S * sinf(x * 0.045f + t * 2.2f);
        spr.fillRect(x, Y(bandTop), 3, wy - bandTop, rowCol[bandTop]);
        spr.fillRect(x, Y(wy), 3, bandBot - wy, rowCol[fg.surf]);
        spr.fillRect(x, Y(wy), 3, 1, TFT_WHITE);
      }
    }

    // seaweed
    static const float weedX[5] = {0.07f, 0.24f, 0.49f, 0.73f, 0.92f};
    for (int i = 0; i < 5; i++) {
      const int segs = 6 + (i * 3) % 4;
      if (!inStrip(fg.sand - segs * 7 * S - 4 * S, fg.sand + 4 * S)) continue;
      for (int s = 0; s < segs; s++) {
        const float cx = weedX[i] * W + sinf(t * 1.5f + s * 0.5f + i) * s * 0.9f * S;
        spr.fillCircle(cx, Y(fg.sand - s * 7 * S), 3.5f * S, hsv565(0.30f + 0.03f * i, 0.8f, 0.55f + 0.04f * s));
      }
    }

    // bubbles
    for (auto &b : bubbles)
      if (inStrip(b.y - 4, b.y + 4)) spr.drawCircle(b.x, Y(b.y), 2.5f * S, TFT_WHITE);

    // fish (the hooked one is drawn with the hook)
    for (int i = 0; i < NFISH; i++)
      if (!(fg.hs == H_HOOKED && i == fg.hooked)) drawFish(fish[i]);

    // boat
    const float bx = fg.boatX, top = fg.surf - 10 * S;
    if (inStrip(rodTipY() - 4, fg.surf + 8 * S)) {
      const uint16_t hull = tft.color565(200, 50, 40), trim = TFT_WHITE;
      spr.fillRect(bx - 28 * S, Y(top), 56 * S, 14 * S, hull);
      spr.fillTriangle(bx - 28 * S, Y(top), bx - 40 * S, Y(top), bx - 28 * S, Y(top + 14 * S), hull);
      spr.fillTriangle(bx + 28 * S, Y(top), bx + 42 * S, Y(top), bx + 28 * S, Y(top + 14 * S), hull);
      spr.fillRect(bx - 40 * S, Y(top), 82 * S, 2 * S + 1, trim);
      // fisher
      spr.fillRect(bx + 4 * S, Y(top - 18 * S), 10 * S, 18 * S, tft.color565(40, 90, 200));
      spr.fillCircle(bx + 9 * S, Y(top - 24 * S), 6 * S, tft.color565(250, 205, 160));
      spr.fillRect(bx + 2 * S, Y(top - 31 * S), 14 * S, 4 * S, TFT_YELLOW);  // hat
      // rod
      const uint16_t rod = tft.color565(120, 70, 30);
      spr.drawLine(bx + 12 * S, Y(top - 10 * S), rodTipX(), Y(rodTipY()), rod);
      spr.drawLine(bx + 12 * S, Y(top - 9 * S), rodTipX(), Y(rodTipY() + 1), rod);
    }

    // line + hook
    if (inStrip(min(rodTipY(), fg.hookY) - 2, max(rodTipY(), fg.hookY) + 8)) {
      spr.drawLine(rodTipX(), Y(rodTipY()), fg.hookX, Y(fg.hookY), fg.hs == H_HOOKED ? TFT_WHITE : TFT_LIGHTGREY);
      spr.drawCircle(fg.hookX, Y(fg.hookY + 3 * S), 3 * S, TFT_SILVER);
    }
    if (fg.hs == H_HOOKED) drawFish(fish[fg.hooked]);

    // HUD
    if (inStrip(0, 60 * S)) {
      spr.drawRoundRect(4, Y(4), 64 * S, 26 * S, 6, TFT_NAVY);
      spr.setTextColor(TFT_NAVY);
      spr.drawCentreString("Menu", 4 + 32 * S, Y(4 + (26 * S - spr.fontHeight(2)) / 2), 2);
      snprintf(buf, sizeof(buf), "Score %d", fg.score);
      spr.setTextColor(TFT_BLACK);
      spr.drawString(buf, 80 * S, Y(6 * S), font);
      snprintf(buf, sizeof(buf), "%d s", timeLeft);
      spr.setTextColor(timeLeft <= 10 ? TFT_RED : TFT_BLACK);
      spr.drawRightString(buf, W - 8, Y(6 * S), font);
      snprintf(buf, sizeof(buf), "Best %d", fg.best);
      spr.setTextColor(TFT_DARKGREY);
      spr.drawString(buf, 80 * S, Y(6 * S + spr.fontHeight(font)), 2);
    }
    // tension meter
    if (fg.hs == H_HOOKED) {
      const int mw = 130 * S, mh = 12 * S, mx = W - mw - 8, my = 40 * S;
      if (inStrip(my, my + mh)) {
        spr.fillRect(mx, Y(my), mw, mh, TFT_DARKGREY);
        spr.fillRect(mx, Y(my), mw * fg.tension / 100, mh, hsv565(0.33f * (1 - fg.tension / 100), 1, 1));
        spr.drawRect(mx, Y(my), mw, mh, TFT_BLACK);
      }
    }

    // +points popup
    if (fg.popAt && now - fg.popAt < 1200) {
      const float py = fg.surf - 20 * S - (now - fg.popAt) / 30.0f * S;
      if (inStrip(py - 30, py + 30)) {
        snprintf(buf, sizeof(buf), "+%d", fg.popPts);
        spr.setTextColor(rainbow(0.0f));
        spr.drawCentreString(buf, fg.popX, Y(py), font);
      }
    }

    // message line
    if (fg.msg && now - fg.msgAt < 1800 && !fg.over) {
      const float my = H * 0.5f;
      if (inStrip(my - 4, my + 30)) {
        spr.setTextColor(TFT_WHITE);
        spr.drawCentreString(fg.msg, W / 2, Y(my), font);
      }
    }

    // game over box
    if (fg.over) {
      const float bw = W * 0.7f, bh = H * 0.5f, bx0 = (W - bw) / 2, by0 = H * 0.3f;
      if (inStrip(by0, by0 + bh)) {
        spr.fillRoundRect(bx0, Y(by0), bw, bh, 12, TFT_NAVY);
        spr.drawRoundRect(bx0, Y(by0), bw, bh, 12, rainbow(0.0f));
        spr.drawRoundRect(bx0 + 1, Y(by0 + 1), bw - 2, bh - 2, 11, rainbow(0.3f));
        spr.setTextColor(rainbow(0.1f));
        spr.drawCentreString(fg.newBest ? "New best!" : "Time's up!", W / 2, Y(by0 + 12 * S), font);
        spr.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "Score %d  (%d fish)", fg.score, fg.caught);
        spr.drawCentreString(buf, W / 2, Y(by0 + bh * 0.42f), font);
        snprintf(buf, sizeof(buf), "Best %d", fg.best);
        spr.drawCentreString(buf, W / 2, Y(by0 + bh * 0.62f), 2);
        spr.setTextColor(TFT_YELLOW);
        spr.drawCentreString("Tap to play again", W / 2, Y(by0 + bh * 0.8f), 2);
      }
    }

    spr.pushSprite(0, oy);
  }
  oy = 0;
}

// ---------------------------------------------------------------- menu
enum AppMode { M_MENU, M_TTT, M_FISH };
static AppMode mode = M_MENU;

static void menuRect(int i, int &x, int &y, int &w, int &h) {
  w = tft.width() * 0.66f;
  h = tft.height() * 0.22f;
  x = (tft.width() - w) / 2;
  y = tft.height() * (0.36f + 0.3f * i);
}

static void menuDraw(bool full) {
  const int W = tft.width();
  const bool big = W >= 480;
  if (full) tft.fillScreen(TFT_BLACK);
  tft.setTextColor(rainbow(0.0f), TFT_BLACK);
  tft.setTextSize(big ? 2 : 1);
  tft.drawCentreString("CYD Games", W / 2, tft.height() * 0.06f, 4);
  tft.setTextSize(1);
  const char *labels[2] = {"Tic Tac Toe", "Fishing"};
  for (int i = 0; i < 2; i++) {
    int x, y, w, h;
    menuRect(i, x, y, w, h);
    const uint16_t c = rainbow(0.33f * i + 0.15f);
    for (int k = 0; k < 3; k++) tft.drawRoundRect(x + k, y + k, w - 2 * k, h - 2 * k, 12 - k, c);
    tft.setTextColor(c, TFT_BLACK);
    tft.drawCentreString(labels[i], W / 2, y + (h - tft.fontHeight(4)) / 2, 4);
  }
  if (full) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("BOOT button: menu    Hold BOOT 2 s: calibrate touch", W / 2, tft.height() - 18, big ? 2 : 1);
  }
}

static void goMenu() {
  mode = M_MENU;
  menuDraw(true);
}

void setup() {
  Serial.begin(115200);
#if HAS_LED_LDR
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  led(false, false, false);
  analogReadResolution(12);
#endif

  tft.init();
  tft.setRotation(1);  // landscape: 320x240 (2.8") or 480x320 (4")
  xptBegin();
  if (!loadCal()) {
    // No saved calibration: rough default (4" panel reads X mirrored). The Calibrate button does it properly.
    const int W = tft.width(), H = tft.height();
#ifdef BOARD_CYD40
    // 4" panel: raw touch axes are the panel's native portrait ones, so in landscape they are swapped and both
    // run backwards (screen x from raw y, screen y from raw x; raw ~300..3800). Worked out from real taps.
    cal.swap = 1;
    cal.cxk = -float(W) / 3500; cal.cx0 = 3800 * float(W) / 3500;
    cal.cyk = -float(H) / 3500; cal.cy0 = 3800 * float(H) / 3500;
#else
    cal.swap = 0;
    cal.cxk = float(W) / 3500;  cal.cx0 = -200 * cal.cxk;
    cal.cyk = float(H) / 3560;  cal.cy0 = -240 * cal.cyk;
#endif
  }
  tft.setSwapBytes(true);  // stripBuf holds normal RGB565 values
  randomSeed(esp_random());
  spiralBegin();
  pinMode(0, INPUT_PULLUP);  // BOOT button
  fishReady = fishBegin();
  layout();
  celebrate(2000);  // spiral splash
  goMenu();
  Serial.printf("CYD games up, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  static uint32_t lastAnim = 0, lastFrame = millis();
  static bool wasDown = false;
  int x = 0, y = 0;
  uint16_t xr, yr;

  const bool down = touchPoint(x, y, xr, yr);
  const bool tap = down && !wasDown;
  wasDown = down;
  led(false, down, false);
  if (tap) Serial.printf("tap raw %4u,%4u -> %3d,%3d\n", xr, yr, x, y);

  // BOOT button (GPIO0): short press = menu, hold 2 s = touch calibration. Works even when touch is misaligned.
  static uint32_t bootSince = 0;
  static bool bootLong = false;
  if (digitalRead(0) == LOW) {
    if (!bootSince) {
      bootSince = millis();
      bootLong = false;
    } else if (!bootLong && millis() - bootSince > 2000) {
      bootLong = true;
      Serial.println("BOOT held: calibrating");
      calibrate();
      goMenu();
    }
  } else if (bootSince) {
    if (!bootLong) {
      Serial.println("BOOT: menu");
      goMenu();
    }
    bootSince = 0;
  }

  switch (mode) {
    case M_MENU:
      if (tap) {
        for (int i = 0; i < 2; i++) {
          int bx, by, bw, bh;
          menuRect(i, bx, by, bw, bh);
          if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (i == 0) {
              mode = M_TTT;
              scoreX = scoreO = scoreD = 0;
              starter = PX;
              newGame();
            } else if (fishReady) {
              mode = M_FISH;
              fishNewRound();
              lastFrame = millis();
            }
          }
        }
      }
      if (mode == M_MENU && millis() - lastAnim > 100) {
        lastAnim = millis();
        menuDraw(false);
      }
      break;

    case M_TTT:
      if (tap) handleTap(x, y);
      if (mode != M_TTT) break;
      if (cpuAt && millis() >= cpuAt && !winner) {
        cpuAt = 0;
        play(cpuChoose());
      }
      if (winner && millis() - resultAt > 1500) {  // show the result, then celebrate and start again
        if (winner != DRAW) celebrate(3500);
        newGame();
      }
      if (millis() - lastAnim > 50) {
        lastAnim = millis();
        drawBoardColours();
        static uint8_t n = 0;
        if (++n % 10 == 0) drawPanel();  // panel colours drift more slowly
      }
      break;

    case M_FISH: {
      if (tap && x < 72 * fg.S && y < 34 * fg.S) {  // Menu button
        goMenu();
        break;
      }
      const uint32_t now = millis();
      const float dt = min(3.0f, (now - lastFrame) / 40.0f);  // 1.0 = one 25 fps frame
      lastFrame = now;
      fishUpdate(dt, down, tap, x, y);
      if (mode == M_FISH) fishRender();
      static uint32_t frames = 0, t0 = millis();
      if (++frames % 200 == 0) Serial.printf("fishing %.1f fps\n", 200000.0f / (millis() - t0)), t0 = millis();
      return;  // no extra delay: rendering paces the loop
    }
  }
  delay(5);
}
