#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <qrcode.h>

#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_CLK 18

#define COLOR_DARKGREY 0x7BEF

#define TOUCH_CLK 14
#define TOUCH_CS 5
#define TOUCH_DIN 13
#define TOUCH_DO 32
#define TOUCH_IRQ 27

// XPT2046 calibration values.  These are a safe starting point for a
// 240x320 display in portrait orientation; see the serial output in
// readTouchPoint() if your panel needs fine calibration.
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3900

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);
WebSocketsClient webSocket;

const char *ssid = "Galaxy A04s 14FE";
const char *password = "dimg3319";
const char *backendHost = "172.27.221.24";
const uint16_t backendPort = 8000;
const char *machineId = "vending01";

struct Product
{
  const char *name;
  int drinkId;
  uint16_t color;
};

Product products[] = {
    {"Coca Cola", 1, ILI9341_RED},
    {"Fanta", 2, ILI9341_YELLOW},
    {"Water", 3, ILI9341_CYAN}};

const int kProductCount = sizeof(products) / sizeof(products[0]);

enum SystemState
{
  STATE_MENU,
  STATE_WAITING_PAYMENT,
  STATE_DISPENSING,
  STATE_DONE,
  STATE_ERROR
};

SystemState systemState = STATE_MENU;
int selectedItem = -1;
String statusMessage = "Touch an item to start";
String currentOrderId;
String currentPaymentUrl;
int currentTotal = 0;
unsigned long lastTouchCheck = 0;
unsigned long lastWsReconnect = 0;
bool touchWasDown = false;

String buildBackendUrl(const char *path)
{
  return String("http://") + backendHost + ":" + String(backendPort) + path;
}

void setTouchPins()
{
  pinMode(TOUCH_CS, OUTPUT);
  pinMode(TOUCH_CLK, OUTPUT);
  pinMode(TOUCH_DIN, OUTPUT);
  pinMode(TOUCH_DO, INPUT_PULLUP);
  pinMode(TOUCH_IRQ, INPUT_PULLUP); // XPT2046 IRQ is active LOW while pressed.
  digitalWrite(TOUCH_CS, HIGH);
  digitalWrite(TOUCH_CLK, LOW);
}

uint8_t softwareSPI_Transfer(uint8_t data)
{
  uint8_t reply = 0;
  for (int i = 7; i >= 0; i--)
  {
    digitalWrite(TOUCH_DIN, (data & (1 << i)) ? HIGH : LOW);
    digitalWrite(TOUCH_CLK, HIGH);
    ets_delay_us(2);
    if (digitalRead(TOUCH_DO))
      reply |= (1 << i);
    digitalWrite(TOUCH_CLK, LOW);
    ets_delay_us(2);
  }
  return reply;
}

uint16_t readTouchAxis(uint8_t command)
{
  digitalWrite(TOUCH_CS, LOW);
  softwareSPI_Transfer(command);
  uint8_t msb = softwareSPI_Transfer(0x00);
  uint8_t lsb = softwareSPI_Transfer(0x00);
  digitalWrite(TOUCH_CS, HIGH);
  return ((msb << 5) | (lsb >> 3)) & 0xFFF;
}

bool readTouchPoint(int &x, int &y)
{
  // Read several times and use the middle value. This rejects the noisy first
  // conversion that many XPT2046-compatible touch controllers produce.
  uint16_t rawX[5];
  uint16_t rawY[5];
  for (int i = 0; i < 5; ++i)
  {
    rawX[i] = readTouchAxis(0x90);
    rawY[i] = readTouchAxis(0xD0);
  }

  for (int i = 0; i < 5; ++i)
  {
    for (int j = i + 1; j < 5; ++j)
    {
      if (rawX[j] < rawX[i])
      {
        uint16_t t = rawX[i];
        rawX[i] = rawX[j];
        rawX[j] = t;
      }
      if (rawY[j] < rawY[i])
      {
        uint16_t t = rawY[i];
        rawY[i] = rawY[j];
        rawY[j] = t;
      }
    }
  }

  const uint16_t filteredX = rawX[2];
  const uint16_t filteredY = rawY[2];
  if (filteredX < 50 || filteredX > 4050 ||
      filteredY < 50 || filteredY > 4050)
    return false;

  // XPT2046 axes are perpendicular to the portrait TFT axes on this wiring.
  x = constrain(map(filteredY, 50, 4050, 240, 0), 0, 239);
  y = constrain(map(filteredX, 50, 4050, 0, 320), 0, 319);
  Serial.printf("TOUCH raw=(%u,%u), screen=(%d,%d), irq=%d\n",
                filteredX, filteredY, x, y, digitalRead(TOUCH_IRQ));
  return true;
}

int getTouchedItem(int x, int y)
{
  if (x < 0 || x > 240 || y < 0 || y > 320)
    return -1;

  const int y0 = 60;
  const int buttonHeight = 60;
  const int gap = 15;

  for (int i = 0; i < kProductCount; i++)
  {
    int top = y0 + i * (buttonHeight + gap);
    int bottom = top + buttonHeight;
    if (y >= top && y <= bottom)
    {
      return i;
    }
  }
  return -1;
}

void drawMenu()
{
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 240, 45, ILI9341_BLUE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print("Smart Vending");

  int y = 60;
  for (int i = 0; i < kProductCount; i++)
  {
    uint16_t fillColor = (i == selectedItem) ? ILI9341_GREEN : COLOR_DARKGREY;
    uint16_t outline = (i == selectedItem) ? ILI9341_WHITE : ILI9341_CYAN;
    tft.fillRoundRect(10, y, 220, 60, 10, fillColor);
    tft.drawRoundRect(10, y, 220, 60, 10, outline);
    tft.setTextColor(products[i].color);
    tft.setTextSize(2);
    tft.setCursor(20, y + 18);
    tft.print(products[i].name);
    y += 75;
  }

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 280);
  tft.print("Scan the QR code after choosing an item.");

  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(10, 300);
  tft.print(statusMessage);
}

void drawQRCode()
{
  if (currentPaymentUrl.length() == 0)
  {
    return;
  }

  const uint8_t qrVersion = 6;

  uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];

  QRCode qrcode;

  qrcode_initText(
      &qrcode,
      qrcodeData,
      qrVersion,
      ECC_LOW,
      currentPaymentUrl.c_str());

  int scale = 4;

  int qrPixels =
      qrcode.size * scale;

  int x0 =
      (240 - qrPixels) / 2;

  int y0 = 55;

  // White QR background
  tft.fillRect(
      x0 - 8,
      y0 - 8,
      qrPixels + 16,
      qrPixels + 16,
      ILI9341_WHITE);

  for (uint8_t y = 0; y < qrcode.size; y++)
  {
    for (uint8_t x = 0; x < qrcode.size; x++)
    {
      bool module =
          qrcode_getModule(
              &qrcode,
              x,
              y);

      if (module)
      {
        tft.fillRect(
            x0 + x * scale,
            y0 + y * scale,
            scale,
            scale,
            ILI9341_BLACK);
      }
    }
  }
}
void drawPaymentScreen()
{
  tft.fillScreen(ILI9341_BLACK);

  tft.fillRect(
      0,
      0,
      240,
      45,
      ILI9341_BLUE);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(45, 12);
  tft.print("SCAN TO PAY");

  // Amount
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);

  String amount =
      String(currentTotal) + " TZS";

  tft.setCursor(75, 25);
  // We intentionally keep the header simple;
  // amount will be displayed below the QR.

  // QR
  drawQRCode();

  // Amount below QR
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);

  tft.setCursor(70, 225);
  tft.print(amount);

  // Instruction
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);

  tft.setCursor(55, 305);
  tft.print("Scan with your phone");
}
void drawStatusScreen()
{
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 240, 45, ILI9341_BLUE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print("Vending Status");

  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(10, 90);
  tft.print(statusMessage);

  if (currentOrderId.length())
  {
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 180);
    tft.print("Order:");
    tft.setCursor(60, 180);
    tft.print(currentOrderId);
  }
}

void updateScreen()
{
  switch (systemState)
  {
  case STATE_MENU:
    drawMenu();
    break;
  case STATE_WAITING_PAYMENT:
    drawPaymentScreen();
    break;
  case STATE_DISPENSING:
  case STATE_DONE:
  case STATE_ERROR:
    drawStatusScreen();
    break;
  }
}

bool createOrder(int itemIndex)
{
  HTTPClient http;

  String url = buildBackendUrl("/order");

  Serial.println();
  Serial.println("================================");
  Serial.println("CREATING ORDER");
  Serial.println("URL: " + url);

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> request;

  request["machine_id"] = machineId;

  JsonArray items = request.createNestedArray("items");

  JsonObject item = items.createNestedObject();

  item["product_id"] = products[itemIndex].drinkId;
  item["quantity"] = 1;

  String payload;
  serializeJson(request, payload);

  Serial.println("Request:");
  Serial.println(payload);

  int statusCode = http.POST(payload);

  Serial.print("HTTP status: ");
  Serial.println(statusCode);

  if (statusCode != HTTP_CODE_OK)
  {
    Serial.println("Order request failed.");
    http.end();
    return false;
  }

  String response = http.getString();

  Serial.println("Backend response:");
  Serial.println(response);

  http.end();

  StaticJsonDocument<4096> doc;

  DeserializationError error =
      deserializeJson(doc, response);

  if (error)
  {
    Serial.print("JSON parsing error: ");
    Serial.println(error.c_str());
    return false;
  }

  String backendStatus =
      doc["status"].as<String>();

  if (backendStatus != "success")
  {
    Serial.println("Backend rejected order.");
    Serial.println(
        doc["message"].as<String>());
    return false;
  }

  currentOrderId =
      doc["order_id"].as<String>();

  currentPaymentUrl =
      doc["payment_url"].as<String>();

  currentTotal =
      doc["total"].as<int>();

  Serial.println();
  Serial.println("ORDER CREATED SUCCESSFULLY");

  Serial.print("Order ID: ");
  Serial.println(currentOrderId);

  Serial.print("Total: ");
  Serial.print(currentTotal);
  Serial.println(" TZS");

  Serial.print("Payment URL: ");
  Serial.println(currentPaymentUrl);

  Serial.println("================================");
  Serial.println();

  return true;
}
void displayQRCode(String url, int total)
{
  Serial.println("Generating QR code...");
  Serial.println(url);

  // QR version 5 gives us enough capacity for our URL
  const int qrVersion = 5;

  uint8_t qrcodeData[qrcode_getBufferSize(qrVersion)];

  QRCode qrcode;

  qrcode_initText(
      &qrcode,
      qrcodeData,
      qrVersion,
      ECC_LOW,
      url.c_str());

  // Clear TFT
  tft.fillScreen(ILI9341_WHITE);

  // Title
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(2);

  tft.setCursor(65, 10);
  tft.println("SCAN TO PAY");

  // Amount
  tft.setTextSize(2);

  String amountText =
      String(total) + " TZS";

  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;

  tft.getTextBounds(
      amountText,
      0,
      0,
      &x1,
      &y1,
      &w,
      &h);

  tft.setCursor(
      (320 - w) / 2,
      40);

  tft.println(amountText);

  // QR dimensions
  int qrSize = qrcode.size;

  // Size of each QR module on the TFT
  int moduleSize = 5;

  int pixelSize =
      qrSize * moduleSize;

  // Center QR horizontally
  int startX =
      (320 - pixelSize) / 2;

  int startY = 75;

  // Draw QR
  for (int y = 0; y < qrSize; y++)
  {
    for (int x = 0; x < qrSize; x++)
    {
      if (qrcode_getModule(
              &qrcode,
              x,
              y))
      {
        tft.fillRect(
            startX + x * moduleSize,
            startY + y * moduleSize,
            moduleSize,
            moduleSize,
            ILI9341_BLACK);
      }
    }
  }

  // Instruction
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextSize(1);

  tft.setCursor(75, 305);
  tft.println("Scan with your phone");

  Serial.println("QR displayed.");
}
void connectWebSocket()
{
  statusMessage = "Connecting backend...";
  updateScreen();

  String wsPath = String("/ws/") + machineId;
  Serial.printf("WebSocket connect to ws://%s:%u%s\n", backendHost, backendPort, wsPath.c_str());
  webSocket.begin(backendHost, backendPort, wsPath.c_str());
  webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                    {
    switch (type) {
      case WStype_CONNECTED:
        Serial.println("WebSocket connected");
        statusMessage = "Backend connected";
        updateScreen();
        break;
      case WStype_DISCONNECTED:
        Serial.println("WebSocket disconnected");
        statusMessage = "Backend disconnected";
        updateScreen();
        break;
      case WStype_TEXT: {
        String message;
        for (size_t i = 0; i < length; i++) {
          message += (char)payload[i];
        }
        StaticJsonDocument<512> msgDoc;
        DeserializationError err = deserializeJson(msgDoc, message);
        if (!err) {
          String action = msgDoc["action"].as<String>();
          if (action == "dispense") {
            int drinkId = msgDoc["drink_id"].as<int>();
            String orderId = msgDoc["order_id"].as<String>();
            statusMessage = "Payment confirmed. Dispensing...";
            systemState = STATE_DISPENSING;
            updateScreen();
            String command = "M" + String(drinkId) + "\n";
            Serial2.print(command);
            Serial.println("Sent to Arduino: " + command);
          }
        }
        break;
      }
      default:
        break;
    } });
  webSocket.setReconnectInterval(5000);
}

void processArduinoResponse()
{
  if (!Serial2.available())
  {
    return;
  }

  String response = Serial2.readStringUntil('\n');
  response.trim();
  if (response.length() == 0)
  {
    return;
  }

  if (response == "DONE")
  {
    statusMessage = "Item dispensed. Thank you!";
    systemState = STATE_DONE;
  }
  else if (response == "EMPTY")
  {
    statusMessage = "Dispense failed: item unavailable.";
    systemState = STATE_ERROR;
  }
  else if (response == "FAILED")
  {
    statusMessage = "Dispense failed: sensor not triggered.";
    systemState = STATE_ERROR;
  }
  else
  {
    statusMessage = "Arduino says: " + response;
    systemState = STATE_ERROR;
  }

  updateScreen();
}

void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
  {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    statusMessage = "Connected to Wi-Fi";
    Serial.print("Wi-Fi connected, IP= ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    statusMessage = "Wi-Fi failed";
    Serial.println("Wi-Fi connection failed");
  }
  updateScreen();
}

void setup()
{
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  tft.begin();
  // The menu and hit areas below use a 240x320 portrait coordinate system.
  tft.setRotation(0);
  setTouchPins();

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED)
  {
    connectWebSocket();
  }

  updateScreen();
}

void loop()
{
  webSocket.loop();
  processArduinoResponse();

  if (WiFi.status() != WL_CONNECTED && millis() - lastWsReconnect > 10000)
  {
    lastWsReconnect = millis();
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED)
    {
      connectWebSocket();
    }
  }

  if (systemState == STATE_MENU && millis() - lastTouchCheck > 50)
  {
    lastTouchCheck = millis();
    int touchX;
    int touchY;
    bool touchDown = readTouchPoint(touchX, touchY);

    // Act only once per physical press. A held finger must be released before
    // it can select another item.
    if (touchDown && !touchWasDown)
    {
      int item = getTouchedItem(touchX, touchY);
      if (item >= 0)
      {
        selectedItem = item;
        statusMessage = String("Selected: ") + products[item].name;
        updateScreen();

        if (createOrder(item))
        {
          systemState = STATE_WAITING_PAYMENT;
          statusMessage = "Scan QR to pay";
        }
        else
        {
          systemState = STATE_ERROR;
          statusMessage = "Unable to create order.";
        }
        updateScreen();
      }
    }
    touchWasDown = touchDown;
  }
}
