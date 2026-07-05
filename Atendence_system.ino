// Full Working Sketch — Touch area = full key cell, fixed touch logic
// (Only touch handling logic changed / clarified; rest kept as in your working project)


#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>   // persistent storage

// IMPORTANT: Include your logo file here
#include "logo.h"

/* ================= AUDIO ================= */
#include "checkin_success.h"
#include "checkout_success.h"
#include "enrollment_success.h"
#include "verification_invalid.h"
#include "verification_valid.h"

#define AUDIO_PIN 25
#define SAMPLE_RATE 16000
#define SAMPLE_DELAY_US (1000000 / SAMPLE_RATE)

/* ================= ADMIN DEFAULTS ================= */
const String ADMIN_DEFAULT_NAME = "ADMIN";
const String ADMIN_DEFAULT_PASS = "1234";

/* ================= WIFI / SERVER ================= */
const char* WIFI_SSID = "wifi_name";
const char* WIFI_PASS = "password";
const char* SERVER_URL = "https://script.google.com/macros/s/fycbyc-KOOsdFqDPcBlk1-pA9r2pmYWqSq8a5HqYIvoX_W0mdNBsLEo3VBJk2tuWyFuHwX/exec";

/* ================= FINGERPRINT ================= */
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger(&mySerial);

/* ================= TOUCH ================= */
#define TOUCH_CS   5
#define TOUCH_IRQ  21
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

/* ================= BUZZER ================= */
#define BUZZER_PIN 27

/* ================= TFT ================= */
TFT_eSPI tft = TFT_eSPI();

/* ================= SCREEN ================= */
#define SCREEN_W 320
#define SCREEN_H 240
#define TEXT_H   45

/* HOME button geometry */
const int HOME_BTN_X = 245;
const int HOME_BTN_Y = 5;
const int HOME_BTN_W = 70;
const int HOME_BTN_H = 30;

/* ================= KEYBOARD CONFIG ================= */
#define ROWS 4
#define COLS 10
#define KEY_Y TEXT_H

// KEY_SPACING defines equal gap around each key
const int KEY_SPACING = 2;

// The following are computed at runtime in setup()
int CELL_W;    // width of each keyboard cell grid
int CELL_H;    // height of each keyboard cell grid
int KEY_W_DRAW;
int KEY_H_DRAW;

/* ================= COLORS ================= */
#define KEY_COLOR   tft.color565(0,255,255) // Cyan
#define KEY_BORDER  TFT_BLACK
#define TEXT_BG     TFT_BLACK

/* ================= DATABASE ================= */
struct User {
  uint8_t id;      // fingerprint model id
  String name;     // employee name
  uint8_t state;   // 0 = OUT, 1 = IN
  String empID;    // employee identifier
};

User users[50];
int userCount = 0;
uint8_t nextID = 1;

/* Preferences (NVS) */
Preferences prefs;

/* ================= ADMIN STORAGE ================= */
String adminNameStored;
String adminPassStored;
uint8_t adminFid = 0;

/* ================= UI STATE ================= */
enum ScreenState {
  HOME,
  ADMIN_AUTH_NAME,
  ADMIN_AUTH_PASS,
  ADMIN_MENU,
  ADMIN_REAUTH_NAME,
  ADMIN_REAUTH_PASS,
  ADMIN_CHANGE_FINGER_ENROLL,
  ADMIN_CHANGE_CRED_NEW_NAME,
  ADMIN_CHANGE_CRED_NEW_PASS,
  DELETE_AUTH_FP,
  DELETE_NAME,
  ENROLL_ADMIN_FINGER,
  ENROLL_NAME,
  ENROLL_ID,
  ENROLL_FINGER
};
ScreenState screen = HOME;

/* ================= INPUT ================= */
String typedText = "";
String tempAdminName = "";
String tempAdminPass = "";
String tempEmpName = "";
String tempEmpId = "";

/* ================= KEYBOARD LAYOUT ================= */
String keys[ROWS][COLS] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l","<"},
  {"!","z","x","c","v","b","n","m","_","OK"},
  {"1","2","3","4","5","6","7","8","9","0"}
};

/* ================= TOUCH ================= */
bool touchLock = false;
void waitRelease() { while (ts.touched()) delay(5); }

/* ================= AUDIO ================= */
void playAudio(const int16_t* audio, unsigned int length) {
  for (unsigned int i = 0; i < length; i++) {
    uint8_t dacValue = (audio[i] >> 8) + 128;
    dacWrite(AUDIO_PIN, dacValue);
    delayMicroseconds(SAMPLE_DELAY_US/1.5);
  }
}

/* ================= UI UTIL ================= */
void beep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(20);
  digitalWrite(BUZZER_PIN, LOW);
}

void drawHeader(String msg) {
  tft.fillRect(0, 0, SCREEN_W, TEXT_H, TEXT_BG);
  tft.setTextColor(TFT_WHITE, TEXT_BG);
  tft.setTextSize(2);
  tft.setCursor(8,15);
  tft.print(msg);
  drawHomeBtn();
}

void drawHeaderr(String msg) {
  tft.fillRect(0, 0, SCREEN_W, TEXT_H, TEXT_BG);
  tft.setTextColor(TFT_WHITE, TEXT_BG);
  tft.setTextSize(2);
  tft.setCursor(8,45);
  tft.print(msg);
  drawHomeBtn();
}

void drawHomeBtn() {
  tft.fillRect(HOME_BTN_X, HOME_BTN_Y, HOME_BTN_W, HOME_BTN_H, KEY_COLOR);
  tft.drawRect(HOME_BTN_X, HOME_BTN_Y, HOME_BTN_W, HOME_BTN_H, KEY_BORDER);

  tft.setTextSize(2);
  String label = "HOME";
  int tw = tft.textWidth(label);
  int fh = tft.fontHeight();
  int tx = HOME_BTN_X + (HOME_BTN_W - tw) / 2;
  int ty = HOME_BTN_Y + (HOME_BTN_H - fh) / 2;
  tft.setTextColor(TFT_BLACK, KEY_COLOR);
  tft.setCursor(tx, ty);
  tft.print(label);
}

bool homeTouched(int x,int y) {
  return (x >= HOME_BTN_X && x <= HOME_BTN_X + HOME_BTN_W && y >= HOME_BTN_Y && y <= HOME_BTN_Y + HOME_BTN_H);
}

bool adminTouched(int x,int y) {
  // showHome draws admin button at: tft.fillRect(110,160-5,100,50,...)
  // that means rect x:110..210, y:155..205
  return (x >= 110 && x <= 210 && y >= 155 && y <= 205);
}

/* ================= KEYBOARD DRAWING ================= */
bool upperCase = true;

void drawKeyLabelRect(int r, int c, const String &label, uint16_t col) {
  if (label.length() == 0) return;

  int rectW = KEY_W_DRAW;
  int rectH = KEY_H_DRAW;

  int x = c * CELL_W + (CELL_W - rectW) / 2;
  int y = KEY_Y + r * CELL_H + (CELL_H - rectH) / 2;

  tft.fillRect(x, y, rectW, rectH, col);
  tft.drawRect(x, y, rectW, rectH, KEY_BORDER);

  String showLabel = label;
  if (label.length() == 1) {
    char ch = label.charAt(0);
    if (isAlpha(ch)) {
      if (upperCase) showLabel = String((char)toupper(ch));
      else showLabel = String((char)tolower(ch));
    }
  } else if (label == "OK") {
    showLabel = "OK";
  }

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, col);

  int tw = tft.textWidth(showLabel);
  int fh = tft.fontHeight();
  int tx = x + (rectW - tw) / 2;
  int ty = y + (rectH - fh) / 2;
  tft.setCursor(tx, ty);
  tft.print(showLabel);
}

void drawKeyboard() {
  tft.fillRect(0, KEY_Y, SCREEN_W, SCREEN_H - KEY_Y, TFT_WHITE);
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      String label = keys[r][c];
      if (label.length() == 0) continue;
      drawKeyLabelRect(r, c, label, KEY_COLOR);
    }
  }
}

/* ================= SHOW HOME (WITH LOGO) ================= */
void showHome() {
  tft.fillScreen(TFT_BLACK);

  int imgW = 55;
  int imgH = 55;

  int xPos = (SCREEN_W - 255) / 2 + 8;
  int yPos = 15;

  int stripX = xPos;
  int stripY = yPos - 2;
  int stripW = imgW + 8 + 160;
  int stripH = imgH + 8;
  if (stripX < 0) stripX = 0;
  if (stripY < TEXT_H) stripY = TEXT_H;
  if (stripX + stripW > SCREEN_W) stripW = SCREEN_W - stripX;
  if (stripY + stripH > SCREEN_H) stripH = stripY + (imgH + 8) - stripY;

  tft.fillRect(stripX, stripY, stripW, stripH, TFT_BLACK);

  tft.setSwapBytes(true);
  tft.pushImage(xPos, yPos, imgW, imgH, logo_img);
  tft.setSwapBytes(false);

  String titleBig = "QuantumCLK";
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  int twBig = tft.textWidth(titleBig);
  int fhBig = tft.fontHeight();

  int titleBigX = xPos + imgW + 8 + 8;
  int titleBigY = yPos + (imgH - fhBig) / 2 - 8;

  if (titleBigX + twBig > SCREEN_W) {
    titleBigX = SCREEN_W - twBig - 4;
    if (titleBigX < xPos + imgW + 4) titleBigX = xPos + imgW + 4;
  }

  tft.setCursor(titleBigX, titleBigY);
  tft.print(titleBig);

  String titleSmall = "Technologies";
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  int twSmall = tft.textWidth(titleSmall);
  int fhSmall = tft.fontHeight();

  int titleSmallX = titleBigX;
  int titleSmallY = titleBigY + fhBig + 2;

  if (titleSmallX + twSmall > SCREEN_W) {
    titleSmallX = SCREEN_W - twSmall - 4;
    if (titleSmallX < xPos + imgW + 4) titleSmallX = xPos + imgW + 4;
  }

  if (titleSmallY + fhSmall > stripY + stripH) {
    titleSmallY = stripY + stripH - fhSmall - 2;
    if (titleSmallY < titleBigY + fhBig + 2) titleSmallY = titleBigY + fhBig + 2;
  }

  tft.setCursor(titleSmallX, titleSmallY);
  tft.print(titleSmall);

  // ENROLL / DELETE / ADMIN Buttons
  tft.fillRect(40,90,100,50, KEY_COLOR);
  tft.drawRect(40,90,100,50, KEY_BORDER);

  tft.fillRect(180,90,100,50, KEY_COLOR);
  tft.drawRect(180,90,100,50, KEY_BORDER);

  tft.fillRect(110,160-5,100,50, KEY_COLOR);
  tft.drawRect(110,160-5,100,50, KEY_BORDER);

  tft.setTextColor(TFT_BLACK, KEY_COLOR);
  tft.setTextSize(2);
  tft.setCursor(60,110);  tft.print("ENROLL");
  tft.setCursor(200,110); tft.print("DELETE");
  tft.setCursor(130,172); tft.print("ADMIN");
}

/* ================= DATABASE HELPERS ================= */
void saveUsers() {
  prefs.begin("users", false); // write
  prefs.putUInt("count", (uint32_t)userCount);
  prefs.putUInt("nextID", (uint32_t)nextID);

  for (int i = 0; i < userCount; i++) {
    String key = "u" + String(i);
    String val = String(users[i].id) + "," + users[i].name + "," + String(users[i].state) + "," + users[i].empID;
    prefs.putString(key.c_str(), val);
  }

  prefs.putString("admin_name", adminNameStored);
  prefs.putString("admin_pass", adminPassStored);
  prefs.putUInt("admin_fid", (uint32_t)adminFid);

  int i = userCount;
  while (true) {
    String key = "u" + String(i);
    if (!prefs.isKey(key.c_str())) break;
    prefs.remove(key.c_str());
    i++;
  }
  prefs.end();
}

void loadUsers() {
  prefs.begin("users", true); // read-only
  userCount = prefs.getUInt("count", 0);
  nextID = (uint8_t)prefs.getUInt("nextID", 1);

  for (int i = 0; i < userCount && i < 50; i++) {
    String key = "u" + String(i);
    String data = prefs.getString(key.c_str(), "");
    if (data.length()) {
      int c1 = data.indexOf(',');
      int c2 = (c1 >= 0) ? data.indexOf(',', c1 + 1) : -1;
      int c3 = (c2 >= 0) ? data.indexOf(',', c2 + 1) : -1;
      if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
        users[i].id = (uint8_t)data.substring(0, c1).toInt();
        users[i].name = data.substring(c1 + 1, c2);
        users[i].state = (uint8_t)data.substring(c2 + 1, c3).toInt();
        users[i].empID = data.substring(c3 + 1);
      } else if (c1 >= 0 && c2 >= 0) {
        users[i].id = (uint8_t)data.substring(0, c1).toInt();
        users[i].name = data.substring(c1 + 1, c2);
        users[i].state = (uint8_t)data.substring(c2 + 1).toInt();
        users[i].empID = "E000";
      } else {
        users[i].id = 0;
        users[i].name = "";
        users[i].state = 0;
        users[i].empID = "E000";
      }
    } else {
      users[i].id = 0;
      users[i].name = "";
      users[i].state = 0;
      users[i].empID = "E000";
    }
  }

  adminNameStored = prefs.getString("admin_name", ADMIN_DEFAULT_NAME);
  adminPassStored = prefs.getString("admin_pass", ADMIN_DEFAULT_PASS);
  adminFid = (uint8_t)prefs.getUInt("admin_fid", 0);
  prefs.end();
}

/* ================= DATABASE ================= */
int findUserIndex(String name) {
  for(int i=0;i<userCount;i++)
    if(users[i].name.equalsIgnoreCase(name))
      return i;
  return -1;
}

String findUser(uint8_t id) {
  for(int i=0;i<userCount;i++)
    if(users[i].id == id)
      return users[i].name;
  return "UNKNOWN";
}

/* ================= NETWORK LOGGING ================= */
void sendLog(const String &employeeName, const String &employeeId, const String &action) {
  if (WiFi.status() != WL_CONNECTED) {
    drawHeader("WiFi reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      delay(200);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    String payload = String("{") +
      "\"device\":\"GATE_1\"," +
      "\"employee_name\":\"" + employeeName + "\"," +
      "\"employee_id\":\"" + employeeId + "\"," +
      "\"action\":\"" + action + "\"" +
      "}";
    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      drawHeader("Logged: " + action);
      delay(600);
    } else {
      drawHeader("Log Failed");
      delay(800);
    }
    http.end();
    drawHeader("");
  } else {
    drawHeader("No WiFi");
    delay(800);
    drawHeader("");
  }
}

void sendEnroll(const String &employeeName, const String &employeeId) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  String payload = String("{") +
    "\"mode\":\"ENROLL\"," +
    "\"employee_name\":\"" + employeeName + "\"," +
    "\"employee_id\":\"" + employeeId + "\"," +
    "\"device\":\"GATE_1\"," +
    "\"admin_verified\":true" +
  "}";
  http.POST(payload);
  http.end();
}

/* ================= ACCESS ================= */
void accessGranted(String name, uint8_t idx) {
  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(40,60);
  String action = "";
  if(users[idx].state == 0) {
    tft.print("CHECK-IN");
    users[idx].state = 1;
    playAudio(checkin_success, checkin_success_len);
    action = "CHECK_IN";
  } else {
    tft.print("CHECK-OUT");
    users[idx].state = 0;
    playAudio(checkout_success, checkout_success_len);
    action = "CHECK_OUT";
  }
  saveUsers();
  tft.setTextSize(2);
  tft.setCursor(40,150);
  tft.print(name);
  sendLog(name, users[idx].empID, action);
  delay(2000);
  showHome();
}

void accessDenied() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(60,120);
  tft.print("DENIED");
  playAudio(verification_invalid, verification_invalid_len);
  delay(3000);
  showHome();
}

/* ================= FINGER ================= */
bool enrollFingerprint(uint8_t id) {
  drawHeader("Place Finger");
  drawHomeBtn();
  while (finger.getImage() != FINGERPRINT_OK) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = map(p.x,200,3800,0,SCREEN_W);
      int y = map(p.y,200,3800,0,SCREEN_H);
      if (homeTouched(x,y)) return false;
    }
    delay(50);
  }
  if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

  drawHeader("Remove Finger");
  drawHomeBtn();
  delay(500);
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = map(p.x,200,3800,0,SCREEN_W);
      int y = map(p.y,200,3800,0,SCREEN_H);
      if (homeTouched(x,y)) return false;
    }
    delay(50);
  }

  drawHeader("Place Again");
  drawHomeBtn();
  while (finger.getImage() != FINGERPRINT_OK) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = map(p.x,200,3800,0,SCREEN_W);
      int y = map(p.y,200,3800,0,SCREEN_H);
      if (homeTouched(x,y)) return false;
    }
    delay(50);
  }
  if (finger.image2Tz(2) != FINGERPRINT_OK) return false;
  if (finger.createModel() != FINGERPRINT_OK) return false;
  return finger.storeModel(id) == FINGERPRINT_OK;
}

bool verifyAdminFingerLoop() {
  if (adminFid == 0) {
    drawHeader("Admin FP not set");
    delay(1200);
    return false;
  }
  drawHeader("Place Admin Thumb");
  drawHomeBtn();
  while (true) {
    if (finger.getImage() == FINGERPRINT_OK) {
      if (finger.image2Tz() != FINGERPRINT_OK) {
        drawHeader("Img->TZ fail");
        delay(300);
        drawHeader("Place Admin Thumb");
        drawHomeBtn();
        continue;
      }
      if (finger.fingerSearch() != FINGERPRINT_OK) {
        playAudio(verification_invalid, verification_invalid_len);
        drawHeader("Wrong Admin Thumb");
        delay(800);
        drawHeader("Place Admin Thumb");
        drawHomeBtn();
        continue;
      }
      if (finger.fingerID == adminFid) {
        playAudio(verification_valid, verification_valid_len);
        drawHeader("Admin Verified");
        delay(600);
        return true;
      } else {
        playAudio(verification_invalid, verification_invalid_len);
        drawHeader("Wrong Admin Thumb");
        delay(800);
        drawHeader("Place Admin Thumb");
        drawHomeBtn();
        continue;
      }
    }
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = map(p.x,200,3800,0,SCREEN_W);
      int y = map(p.y,200,3800,0,SCREEN_H);
      if (homeTouched(x,y)) {
        drawHeader("Cancelled");
        delay(400);
        return false;
      }
    }
    delay(50);
  }
}

/* ================= DELETE ================= */
void deleteUser(String name) {
  int idx = findUserIndex(name);
  if (idx < 0) {
    drawHeader("User Not Found");
    delay(1500);
    typedText = "";
    screen = HOME;
    showHome();
    return;
  }
  uint8_t fpId = users[idx].id;
  String empIdToLog = users[idx].empID;
  String nameToLog = users[idx].name;

  int8_t fpResp = finger.deleteModel(fpId);
  if (fpResp == FINGERPRINT_OK) {
    playAudio(checkout_success, checkout_success_len);
  } else if (fpResp == FINGERPRINT_NOTFOUND) {
    drawHeader("FP not on sensor");
    delay(900);
  } else {
    drawHeader("FP del error");
    delay(1200);
    typedText = "";
    screen = HOME;
    showHome();
    return;
  }

  for (int i = idx; i < userCount - 1; i++) {
    users[i] = users[i + 1];
  }
  userCount--;
  sendLog(nameToLog, empIdToLog, "DELETE");
  saveUsers();
  drawHeader("Deleted: " + name);
  delay(1500);
  typedText = "";
  screen = HOME;
  showHome();
}

String matchFingerprint(uint8_t &idx) {
  if (finger.getImage() != FINGERPRINT_OK) return "";
  if (finger.image2Tz() != FINGERPRINT_OK) return "";
  if (finger.fingerSearch() != FINGERPRINT_OK) return "DENIED";
  String name = findUser(finger.fingerID);
  if (name == "UNKNOWN") return "DENIED";
  for (int i = 0; i < userCount; i++) {
    if (users[i].id == finger.fingerID) {
      idx = i;
      break;
    }
  }
  playAudio(verification_valid, verification_valid_len);
  return name;
}

/* ================= Admin menu button geometry ================= */
int adminBtn1X=0, adminBtn1Y=0, adminBtn1W=0, adminBtn1H=0;
int adminBtn2X=0, adminBtn2Y=0, adminBtn2W=0, adminBtn2H=0;

/* ================= SETUP ================= */
void setup() {
  pinMode(BUZZER_PIN,OUTPUT);
  tft.init();
  tft.setRotation(3);   // 180° display if you want upside-down
  ts.begin();
  ts.setRotation(3);
  mySerial.begin(57600,SERIAL_8N1,17,16);
  finger.begin(57600);

  // If you were using invertDisplay previously:
  tft.invertDisplay(true);

  CELL_W = SCREEN_W / COLS;
  CELL_H = (SCREEN_H - TEXT_H) / ROWS;

  KEY_W_DRAW = CELL_W - (KEY_SPACING * 2);
  KEY_H_DRAW = CELL_H - (KEY_SPACING * 2);

  if (KEY_W_DRAW < 2) KEY_W_DRAW = 2;
  if (KEY_H_DRAW < 2) KEY_H_DRAW = 2;

  drawHeader("WiFi connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    drawHeader("WiFi OK");
    delay(800);
  } else {
    drawHeader("WiFi Failed");
    delay(1000);
  }

  loadUsers();
  showHome();
}

void showAdminMenu() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("Admin Menu");

  String label1 = "CHG FINGER";
  String label2 = "CHG CRED";

  int btnTextSize = 3;
  tft.setTextSize(btnTextSize);
  int tw1 = tft.textWidth(label1);
  int tw2 = tft.textWidth(label2);
  int fh = tft.fontHeight();

  int padX = 20;
  int padY = 12;
  int bw = max(tw1, tw2) + padX * 2;
  int bh = fh + padY * 2;
  int bx = (SCREEN_W - bw) / 2;
  int spacing = 16;
  int totalH = bh * 2 + spacing;
  int startY = (SCREEN_H - totalH) / 2 + TEXT_H/2;

  adminBtn1X = bx;
  adminBtn1Y = startY;
  adminBtn1W = bw;
  adminBtn1H = bh;

  adminBtn2X = bx;
  adminBtn2Y = startY + bh + spacing;
  adminBtn2W = bw;
  adminBtn2H = bh;

  tft.fillRect(adminBtn1X, adminBtn1Y, adminBtn1W, adminBtn1H, KEY_COLOR);
  tft.drawRect(adminBtn1X, adminBtn1Y, adminBtn1W, adminBtn1H, KEY_BORDER);

  tft.fillRect(adminBtn2X, adminBtn2Y, adminBtn2W, adminBtn2H, KEY_COLOR);
  tft.drawRect(adminBtn2X, adminBtn2Y, adminBtn2W, adminBtn2H, KEY_BORDER);

  tft.setTextColor(TFT_BLACK, KEY_COLOR);
  int textX1 = adminBtn1X + (adminBtn1W - tw1) / 2;
  int textY1 = adminBtn1Y + (adminBtn1H - fh) / 2;
  tft.setCursor(textX1, textY1);
  tft.print(label1);

  int textX2 = adminBtn2X + (adminBtn2W - tw2) / 2;
  int textY2 = adminBtn2Y + (adminBtn2H - fh) / 2;
  tft.setCursor(textX2, textY2);
  tft.print(label2);

  tft.setTextSize(2);
  drawHomeBtn();
}

/* ================= LOOP ================= */
void loop() {
  // Finger-check on HOME screen (unchanged)
  if(screen == HOME) {
    uint8_t idx=0;
    String res = "";
    if (finger.getImage() == FINGERPRINT_OK) {
      res = matchFingerprint(idx);
    }
    if(res=="DENIED") accessDenied();
    else if(res.length()) accessGranted(res,idx);
  }

  // If admin enroll/re-auth flows that wait for admin fingerprint, they internally poll ts.touched when needed.
  if (screen == ENROLL_ADMIN_FINGER) {
    if (adminFid == 0) {
      drawHeader("Set Admin FP in Admin Menu");
      drawHomeBtn();
      delay(1200);
      screen = HOME;
      showHome();
    } else {
      bool ok = verifyAdminFingerLoop();
      if (!ok) {
        screen = HOME;
        showHome();
      } else {
        screen = ENROLL_NAME;
        typedText = "";
        tft.fillScreen(TFT_BLACK);
        drawHeader("ENTER EMP NAME → OK");
        drawHomeBtn();
        drawKeyboard();
      }
    }
  }

  if (screen == DELETE_AUTH_FP) {
    drawHomeBtn();
    bool ok = verifyAdminFingerLoop();
    if (!ok) {
      screen = HOME;
      showHome();
    } else {
      screen = DELETE_NAME;
      typedText = "";
      tft.fillScreen(TFT_BLACK);
      drawHeader("Delete Name → OK");
      drawHomeBtn();
      drawKeyboard();
    }
  }

  // --- TOUCH HANDLING (single place, robust) ---
  // If no touch, nothing to do
  if (!ts.touched()) {
    touchLock = false; // ready for next press
    return;
  }

  // If we've already processed this press, wait until release
  if (touchLock) {
    waitRelease();
    touchLock = false;
    return;
  }

  // New press detected
  touchLock = true;
  TS_Point p = ts.getPoint();
  int x = map(p.x,200,3800,0,SCREEN_W);
  int y = map(p.y,200,3800,0,SCREEN_H);
  beep();

  // HOME / ENROLL / DELETE / ADMIN buttons on home
  if (screen == HOME) {
    if (x >= 40 && x <= 140 && y >= 90 && y <= 140) {
      // ENROLL button pressed
      screen = ENROLL_ADMIN_FINGER;
      tft.fillScreen(TFT_BLACK);
      drawHeader("Admin Thumb → Cancel: HOME");
      drawHomeBtn();
      waitRelease();
      touchLock = false;
      return;
    } else if (x >= 180 && x <= 280 && y >= 90 && y <= 140) {
      // DELETE button pressed
      screen = DELETE_AUTH_FP;
      tft.fillScreen(TFT_BLACK);
      drawHeader("Admin Thumb to Delete");
      drawHomeBtn();
      waitRelease();
      touchLock = false;
      return;
    } else if (adminTouched(x,y)) {
      // ADMIN pressed
      screen = ADMIN_AUTH_NAME;
      typedText="";
      tft.fillScreen(TFT_BLACK);
      drawHeader("Admin Name → OK");
      drawHomeBtn();
      drawKeyboard();
      waitRelease();
      touchLock = false;
      return;
    }
  }

  // HOME button (works on any screen)
  if (homeTouched(x,y)) {
    screen = HOME;
    showHome();
    waitRelease();
    touchLock = false;
    return;
  }

  // ADMIN menu buttons
  if (screen == ADMIN_MENU) {
    if (x >= adminBtn1X && x <= (adminBtn1X + adminBtn1W) &&
        y >= adminBtn1Y && y <= (adminBtn1Y + adminBtn1H)) {
      screen = ADMIN_REAUTH_NAME;
      typedText = "";
      tft.fillScreen(TFT_BLACK);
      drawHeader("Admin Name → OK");
      drawHomeBtn();
      drawKeyboard();
      waitRelease();
      touchLock = false;
      return;
    } else if (x >= adminBtn2X && x <= (adminBtn2X + adminBtn2W) &&
               y >= adminBtn2Y && y <= (adminBtn2Y + adminBtn2H)) {
      screen = ADMIN_CHANGE_CRED_NEW_NAME;
      typedText = "";
      tft.fillScreen(TFT_BLACK);
      drawHeader("New Admin Name → OK");
      drawHomeBtn();
      drawKeyboard();
      waitRelease();
      touchLock = false;
      return;
    }
  }

  // KEYBOARD: treat the whole cell (CELL_W x CELL_H) as hitbox
    // KEYBOARD: treat only the drawn key rectangle as hitbox (KEY_W_DRAW x KEY_H_DRAW)
  if (y >= KEY_Y) {
    int col = x / CELL_W;
    int row = (y - KEY_Y) / CELL_H;

    // clamp indices
    if (col < 0) col = 0;
    if (col >= COLS) col = COLS - 1;
    if (row < 0) row = 0;
    if (row >= ROWS) row = ROWS - 1;

    // compute the exact drawn key rectangle within the cell
    int keyX = col * CELL_W + (CELL_W - KEY_W_DRAW) / 2;
    int keyY = KEY_Y + row * CELL_H + (CELL_H - KEY_H_DRAW) / 2;
    int keyW = KEY_W_DRAW;
    int keyH = KEY_H_DRAW;

    // If touch is outside the drawn key rectangle, ignore it
    if (!(x >= keyX && x <= (keyX + keyW) && y >= keyY && y <= (keyY + keyH))) {
      // not on the visible key box
      waitRelease();
      touchLock = false;
      return;
    }

    // now determine which label this key is (same col/row as before)
    String label = keys[row][col];
    if (label.length() == 0) {
      waitRelease();
      touchLock = false;
      return;
    }

    // Process key press (same logic as before)
    if (label == "<") {
      // delete once immediately
      if (typedText.length()) typedText.remove(typedText.length()-1);

      // draw masked or plain header
      if (screen == ADMIN_AUTH_PASS || screen == ADMIN_REAUTH_PASS || screen == ADMIN_CHANGE_CRED_NEW_PASS) {
        String mask = "";
        for (int i = 0; i < typedText.length(); ++i) mask += '*';
        drawHeader(mask);
      } else {
        drawHeader(typedText);
      }

      // continuous delete while holding
      while (ts.touched()) {
        delay(120);
        if (typedText.length()) {
          typedText.remove(typedText.length()-1);
          if (screen == ADMIN_AUTH_PASS || screen == ADMIN_REAUTH_PASS || screen == ADMIN_CHANGE_CRED_NEW_PASS) {
            String mask = "";
            for (int i = 0; i < typedText.length(); ++i) mask += '*';
            drawHeader(mask);
          } else {
            drawHeader(typedText);
          }
        }
      }
      waitRelease();
      touchLock = false;
      return;
    }
    else if (label == "_") {
      typedText += ' ';
    }
    else if (label == "OK") {
      // handle OK based on screen (same as original code)
      if (screen == ADMIN_AUTH_NAME) {
        tempAdminName = typedText;
        typedText = "";
        screen = ADMIN_AUTH_PASS;
        tft.fillScreen(TFT_WHITE);
        drawHeader("Admin Password → OK");
        drawHomeBtn();
        drawKeyboard();
      }
      else if (screen == ADMIN_AUTH_PASS) {
        tempAdminPass = typedText;
        typedText = "";
        if (tempAdminName.equalsIgnoreCase(adminNameStored) && tempAdminPass == adminPassStored) {
          tempAdminName = "";
          tempAdminPass = "";
          screen = ADMIN_MENU;
          showAdminMenu();
        } else {
          tft.fillScreen(TFT_RED);
          tft.setTextColor(TFT_WHITE);
          tft.setTextSize(2);
          tft.setCursor(60,120);
          tft.print("WRONG DETAILS");
          delay(1500);
          screen = HOME;
          showHome();
        }
      }
      else if (screen == ADMIN_REAUTH_NAME) {
        tempAdminName = typedText;
        typedText = "";
        screen = ADMIN_REAUTH_PASS;
        tft.fillScreen(TFT_BLACK);
        drawHeader("Admin Password → OK");
        drawHomeBtn();
        drawKeyboard();
      }
      else if (screen == ADMIN_REAUTH_PASS) {
        tempAdminPass = typedText;
        typedText = "";
        if (tempAdminName.equalsIgnoreCase(adminNameStored) && tempAdminPass == adminPassStored) {
          tempAdminName = "";
          tempAdminPass = "";
          screen = ADMIN_CHANGE_FINGER_ENROLL;
          tft.fillScreen(TFT_BLACK);
          drawHeaderr("Enroll New Admin Thumb    (HOME to cancel)");
          drawHomeBtn();
        } else {
          tft.fillScreen(TFT_RED);
          tft.setTextColor(TFT_WHITE);
          tft.setTextSize(2);
          tft.setCursor(60,120);
          tft.print("WRONG DETAILS");
          delay(1500);
          screen = ADMIN_MENU;
          showAdminMenu();
        }
      }
      else if (screen == ADMIN_CHANGE_FINGER_ENROLL) {
        screen = ADMIN_MENU;
        showAdminMenu();
      }
      else if (screen == ADMIN_CHANGE_CRED_NEW_NAME) {
        tempAdminName = typedText;
        typedText = "";
        screen = ADMIN_CHANGE_CRED_NEW_PASS;
        tft.fillScreen(TFT_BLACK);
        drawHeader("New Admin Pass → OK");
        drawHomeBtn();
        drawKeyboard();
      }
      else if (screen == ADMIN_CHANGE_CRED_NEW_PASS) {
        tempAdminPass = typedText;
        typedText = "";
        adminNameStored = tempAdminName;
        adminPassStored = tempAdminPass;
        tempAdminName = "";
        tempAdminPass = "";
        saveUsers();
        drawHeader("Admin Cred Updated");
        delay(900);
        screen = HOME;
        showHome();
      }
      else if (screen == ENROLL_NAME) {
        tempEmpName = typedText;
        typedText = "";
        screen = ENROLL_ID;
        tft.fillScreen(TFT_BLACK);
        drawHeader("ENTER EMP ID → OK");
        drawHomeBtn();
        drawKeyboard();
      }
      else if (screen == ENROLL_ID) {
        tempEmpId = typedText;
        typedText = "";
        screen = ENROLL_FINGER;
        tft.fillScreen(TFT_BLACK);
        drawHeader("Enroll Thumb → Cancel: HOME");
        drawHomeBtn();
      }
      else if (screen == ENROLL_FINGER) {
        screen = HOME;
        showHome();
      }
      else if (screen == DELETE_NAME) {
        deleteUser(typedText);
      }

      waitRelease();
      touchLock = false;
      return;
    }
    else if (label == "!") {
      upperCase = !upperCase;
      drawKeyboard();
      waitRelease();
      touchLock = false;
      return;
    }
    else {
      // normal character key (single append)
      if (label.length() == 1) {
        char ch = label.charAt(0);
        if (isAlpha(ch)) {
          char outch = upperCase ? toupper(ch) : tolower(ch);
          typedText += outch;
        } else {
          typedText += ch;
        }
      } else {
        typedText += label;
      }
    }

    // update header (masked for password screens)
    if (screen == ADMIN_AUTH_PASS || screen == ADMIN_REAUTH_PASS || screen == ADMIN_CHANGE_CRED_NEW_PASS) {
      String mask = "";
      for (int i = 0; i < typedText.length(); ++i) mask += '*';
      drawHeader(mask);
    } else {
      drawHeader(typedText);
    }

    // wait until release to avoid multiple rapid triggers
    waitRelease();
    touchLock = false;
    return;
  }

  // default fallback: wait release and clear lock
  waitRelease();
  touchLock = false;
}
