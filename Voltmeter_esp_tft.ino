// -----------------------------------------------------------------------------
// FILE: Voltmeter_kV_TFT.ino
// VER : v1.0.0
//
// VERSION HISTORY:
// v1.0.0  - Tagged for Release. File renamed. Added Dynamic Attenuation logic.
// v0.4.36 - Mode change triggers screen clear. Added color shift (Amber/Orange).
// v0.4.34 - Added detailed math scaling equations in comments.
// v0.4.32 - Locked MODE_PIN to GPIO 8 (Silkscreen A5). 
// v0.4.30 - Implemented Dynamic ADC Attenuation (0dB for kV, 11dB for V).
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>

// CONFIG
constexpr char VERSION_STR[] = "v1.0.0";
constexpr int VOLT_PIN = A0;   
constexpr int MODE_PIN = 8;    // Silkscreen A5
constexpr int NEO_PIN  = 40; 
constexpr float ADC_MAX_CODE  = 65535.0f;

// RANGE AND SCALING CONSTANTS
constexpr float FS_V_MODE  = 3.3f;   
constexpr float FS_KV_MODE = 0.5f;   
constexpr float HV_DIVIDER_RATIO = 10000.0f; 

constexpr float ALPHA         = 0.50f;
constexpr float REDRAW_THRESHOLD = 0.001f;

// Timing
constexpr uint32_t MEAS_INTERVAL_MS = 17;
constexpr uint32_t SER_INTERVAL_MS  = 101;
constexpr uint32_t DISP_INTERVAL_MS = 250; 
constexpr uint32_t HB_PERIOD_MS     = 1000;
constexpr uint32_t HB_ON_MS         = 15;

// Geometry
constexpr int BAR_Y = 32, BAR_H = 12, AXIS_Y = 46, TICK_H = 4;
constexpr int BAR_X_START = 10, BAR_X_END = 230;

// PALETTE & THEME
constexpr uint16_t TAN_PALLADIAN = 0xF6B2;
constexpr uint16_t ORA_BURNING   = 0xFD20; // Used for kV (Safety Orange)
constexpr uint16_t ORA_AMBER     = 0xFFE0; // Used for V (Mellow Amber)
constexpr uint16_t RST_MID_FOOTER = 0xB306;
constexpr uint16_t RST_COPPER    = 0xB440;
constexpr uint16_t COL_BLACK     = 0x0000;

struct ColorTheme {
  uint16_t bg, primary, secondary, accent, footer, erase;
};
const ColorTheme Theme = { COL_BLACK, ORA_BURNING, ORA_AMBER, RST_COPPER, RST_MID_FOOTER, COL_BLACK };

// GLOBALS
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;
Adafruit_NeoPixel pixel(1, NEO_PIN, NEO_GRB + NEO_KHZ800);

float filtered_V  = 0.0f;
float last_disp_V = -99.0f;
bool use_hv_mode  = false;
bool last_hv_mode = false;
char display_filename_str[64];
char msg_buf[128];

inline uint32_t ms_since(uint32_t start) { return millis() - start; }
inline uint32_t us_since(uint32_t start) { return micros() - start; }

// FORWARD DEFS
void drawStaticUI();

// HELPERS
void echo(const char *msg) { 
  if (Serial) Serial.print(msg); 
}

// LOGIC AND DRAWING
float measureVoltage() {
  constexpr int N_SAMPLES = 8;
  constexpr uint32_t SAMPLE_DUR_US = 50;
  uint32_t sum = 0;
  for (int i = 0; i < N_SAMPLES; i++) {
    uint32_t start = micros();
    sum += analogRead(VOLT_PIN);
    while (us_since(start) < SAMPLE_DUR_US);
  }
  float avg_code = (float)sum / (float)N_SAMPLES;
  float scale_ref = use_hv_mode ? 1.1f : 3.3f; 
  return (avg_code / ADC_MAX_CODE) * scale_ref;
}

void drawBarGraph(float val, uint16_t color) {
  float fs_limit = use_hv_mode ? FS_KV_MODE : FS_V_MODE;
  float percent = val / fs_limit;
  if (percent > 1.0f) percent = 1.0f;
  if (percent < 0.0f) percent = 0.0f;
  int active_px = (int)(percent * 220);
  for (int x = BAR_X_START; x < BAR_X_END; x += 5) {
    uint16_t barColor = (x - BAR_X_START < active_px) ? color : 0x2102;
    tft.fillRect(x, BAR_Y, 4, BAR_H, barColor);
  }
}

void updateMainUI(float val) {
  char sbuf[24];
  uint16_t activeColor = use_hv_mode ? Theme.primary : Theme.secondary;
  
  /* MATH SCALING EQUATIONS:
   * kV mode: val * (Divider 10000 / 1000 to kilo) = val * 10.0
   * V mode:  val * 1.0 (Direct)
   */
  float displayVal = use_hv_mode ? (val * 10.0f) : val;

  // 1. Large Number
  dtostrf(displayVal, 5, 2, sbuf);
  u8g2.setFont(u8g2_font_inb27_mn); 
  u8g2.setForegroundColor(activeColor);
  u8g2.setBackgroundColor(Theme.bg); 
  int valWidth = u8g2.getUTF8Width(sbuf);
  int xVal = (240 - valWidth - 45) / 2; 
  u8g2.setCursor(xVal, 100);
  u8g2.print(sbuf);

  // 2. Unit Area Clear + Draw
  int unitX = xVal + valWidth + 6;
  tft.fillRect(unitX, 70, 45, 35, Theme.bg); 
  
  u8g2.setFont(u8g2_font_9x15_tr); 
  u8g2.setForegroundColor(activeColor);
  u8g2.setBackgroundColor(Theme.bg);
  u8g2.setCursor(unitX, 100); 
  u8g2.print(use_hv_mode ? "kV" : "V");

  // 3. Pin Voltage Info
  if (use_hv_mode) {
    sprintf(sbuf, "%.3fV", val);
    u8g2.setFont(u8g2_font_9x15_tr);
    u8g2.setForegroundColor(TAN_PALLADIAN);
    u8g2.setBackgroundColor(Theme.bg);
    u8g2.setCursor(BAR_X_START, 20);
    u8g2.print(sbuf);
  } else {
    tft.fillRect(BAR_X_START, 5, 100, 20, Theme.bg);
  }
  drawBarGraph(val, activeColor);
}

void drawStaticUI() {
  tft.drawFastHLine(BAR_X_START, AXIS_Y, 220, Theme.accent);
  for (int i = 0; i <= 10; i++) tft.drawFastVLine(BAR_X_START + (i * 22), AXIS_Y, TICK_H, Theme.accent);
  u8g2.setFont(u8g2_font_9x15_tr);
  u8g2.setForegroundColor(Theme.footer);
  u8g2.setBackgroundColor(Theme.bg);
  u8g2.setCursor(2, 133);
  u8g2.print(display_filename_str);
  u8g2.setCursor(240 - u8g2.getUTF8Width(VERSION_STR) - 2, 133);
  u8g2.print(VERSION_STR);
}

void printVolts() {
  sprintf(msg_buf, "%7u, %6.4f\n", millis(), filtered_V);
  echo(msg_buf);
}

void dispVolts() {
  use_hv_mode = (digitalRead(MODE_PIN) == LOW);
  if (use_hv_mode != last_hv_mode) {
     analogSetAttenuation(use_hv_mode ? ADC_0db : ADC_11db);
     last_hv_mode = use_hv_mode;
     tft.fillScreen(Theme.bg); 
     drawStaticUI();           
     updateMainUI(filtered_V); 
  }
  if (fabs(filtered_V - last_disp_V) >= REDRAW_THRESHOLD) {
    last_disp_V = filtered_V;
    updateMainUI(last_disp_V); 
  }
}

void initHardware() {
  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  analogReadResolution(16);
  analogSetAttenuation(ADC_11db); 
  pixel.begin();
  pixel.setBrightness(0);
  pixel.show();
  String path = String(__FILE__);
  int i = max((int)path.lastIndexOf("\\"), (int)path.lastIndexOf("/"));
  strncpy(display_filename_str, path.substring(i + 1).c_str(), sizeof(display_filename_str));
}

void initDisplay() {
#if defined(TFT_I2C_POWER)
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
#endif
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(Theme.bg);
  u8g2.begin(tft);
}

void initSerial() {
  Serial.end(); 
  delay(200);
  Serial.begin(115200);
  uint32_t wait = millis();
  while (!Serial && (millis() - wait < 2500)) digitalWrite(LED_BUILTIN, (millis() / 50) % 2);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.flush();
  echo("\n# ------------------------------------------------\n");
  sprintf(msg_buf, "# FILE:    %s\n", display_filename_str); echo(msg_buf);
  sprintf(msg_buf, "# VER:     %s\n", VERSION_STR); echo(msg_buf);
  sprintf(msg_buf, "# MODE PIN: 8 (Silkscreen A5)\n"); echo(msg_buf);
  echo("# COLUMNS: ms, volts_filtered\n");
  echo("# ------------------------------------------------\n");
}

void setup() {
  initHardware();
  initDisplay();
  drawStaticUI();
  initSerial();
}

void loop() {
  static uint32_t t_meas = 0, t_disp = 0, t_ser = 0, t_hb = 0;
  if (ms_since(t_meas) >= MEAS_INTERVAL_MS) {
    t_meas = millis();
    filtered_V = (ALPHA * measureVoltage()) + (1.0f - ALPHA) * filtered_V;
  }
  if (ms_since(t_ser) >= SER_INTERVAL_MS) {
    t_ser = millis();
    printVolts();
  }
  if (ms_since(t_disp) >= DISP_INTERVAL_MS) {
    t_disp = millis();
    dispVolts();
  }
  if (ms_since(t_hb) >= HB_PERIOD_MS) {
    t_hb = millis();
    digitalWrite(LED_BUILTIN, HIGH);
  }
  if (digitalRead(LED_BUILTIN) && ms_since(t_hb) >= HB_ON_MS) digitalWrite(LED_BUILTIN, LOW);
}