#include <Arduino.h>
#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <qrcode.h>

// ================= TFT =================

#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_CLK 18

#define COLOR_DARKGREY 0x7BEF

// ================= TOUCH =================

#define TOUCH_CLK 14
#define TOUCH_CS 5
#define TOUCH_DIN 13
#define TOUCH_DO 32
#define TOUCH_IRQ 27

#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3900

// ================= DISPLAY =================

// ST7789 display
Adafruit_ST7789 tft = Adafruit_ST7789(
    TFT_CS,
    TFT_DC,
    TFT_RST);

WebSocketsClient webSocket;

// ================= WIFI =================

const char *ssid = "Galaxy A04s 14FE";
const char *password = "dimg3319";

// ================= BACKEND =================

const char *backendHost = "10.32.117.24";
const uint16_t backendPort = 8000;

const char *machineId = "vending01";

// ================= PRODUCTS =================

struct Product
{
  const char *name;
  int drinkId;
  uint16_t color;
};

Product products[] =
    {
        {"Coca Cola", 1, ST77XX_RED},
        {"Fanta", 2, ST77XX_YELLOW},
        {"Water", 3, ST77XX_CYAN}};

const int kProductCount =
    sizeof(products) / sizeof(products[0]);

// ================= STATES =================

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

// =================================================
// BACKEND URL
// =================================================

String buildBackendUrl(const char *path)
{
  return String("http://") +
         backendHost +
         ":" +
         String(backendPort) +
         path;
}

// =================================================
// TOUCH SETUP
// =================================================

void setTouchPins()
{
  pinMode(TOUCH_CS, OUTPUT);
  pinMode(TOUCH_CLK, OUTPUT);
  pinMode(TOUCH_DIN, OUTPUT);

  pinMode(TOUCH_DO, INPUT_PULLUP);
  pinMode(TOUCH_IRQ, INPUT_PULLUP);

  digitalWrite(TOUCH_CS, HIGH);
  digitalWrite(TOUCH_CLK, LOW);
}

// =================================================
// SOFTWARE SPI FOR TOUCH
// =================================================

uint8_t softwareSPI_Transfer(uint8_t data)
{
  uint8_t reply = 0;

  for (int i = 7; i >= 0; i--)
  {
    digitalWrite(
        TOUCH_DIN,
        (data & (1 << i)) ? HIGH : LOW);

    digitalWrite(TOUCH_CLK, HIGH);

    ets_delay_us(2);

    if (digitalRead(TOUCH_DO))
    {
      reply |= (1 << i);
    }

    digitalWrite(TOUCH_CLK, LOW);

    ets_delay_us(2);
  }

  return reply;
}

// =================================================
// READ TOUCH AXIS
// =================================================

uint16_t readTouchAxis(uint8_t command)
{
  digitalWrite(TOUCH_CS, LOW);

  softwareSPI_Transfer(command);

  uint8_t msb =
      softwareSPI_Transfer(0x00);

  uint8_t lsb =
      softwareSPI_Transfer(0x00);

  digitalWrite(TOUCH_CS, HIGH);

  return ((msb << 5) | (lsb >> 3)) & 0xFFF;
}

// =================================================
// READ TOUCH POINT
// =================================================

bool readTouchPoint(int &x, int &y)
{
  // XPT2046 IRQ is LOW when screen is touched
  if (digitalRead(TOUCH_IRQ) == HIGH)
  {
    return false;
  }

  uint16_t rawX[3];
  uint16_t rawY[3];

  // Read 3 samples
  for (int i = 0; i < 3; i++)
  {
    rawX[i] = readTouchAxis(0x90);
    rawY[i] = readTouchAxis(0xD0);
  }

  // Use average
  uint16_t filteredX =
      (rawX[0] + rawX[1] + rawX[2]) / 3;

  uint16_t filteredY =
      (rawY[0] + rawY[1] + rawY[2]) / 3;

  /*
    Touch mapping for:
    ST7789
    240 x 320
    tft.setRotation(2)
  */

  x = map(
      filteredY,
      TOUCH_Y_MIN,
      TOUCH_Y_MAX,
      239,
      0);

  y = map(
      filteredX,
      TOUCH_X_MIN,
      TOUCH_X_MAX,
      319,
      0);

  x = constrain(x, 0, 239);
  y = constrain(y, 0, 319);

  Serial.printf(
      "Touch -> X=%d Y=%d | RawX=%d RawY=%d\n",
      x,
      y,
      filteredX,
      filteredY);

  return true;
}

// =================================================
// GET TOUCHED PRODUCT
// =================================================

int getTouchedItem(int x, int y)
{
  if (x < 0 || x >= 240 ||
      y < 0 || y >= 320)
  {
    return -1;
  }

  const int y0 = 60;
  const int buttonHeight = 60;
  const int gap = 15;

  for (int i = 0; i < kProductCount; i++)
  {
    int top =
        y0 + i * (buttonHeight + gap);

    int bottom =
        top + buttonHeight;

    if (y >= top &&
        y <= bottom)
    {
      return i;
    }
  }

  return -1;
}

// =================================================
// DRAW MENU
// =================================================

void drawMenu()
{
  tft.fillScreen(ST77XX_BLACK);

  // Header
  tft.fillRect(
      0, 0,
      240, 45,
      ST77XX_BLUE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  tft.setCursor(40, 12);
  tft.print("SMART VENDING");

  // Product buttons
  int y = 60;

  for (int i = 0;
       i < kProductCount;
       i++)
  {
    uint16_t fillColor =
        (i == selectedItem)
            ? ST77XX_GREEN
            : COLOR_DARKGREY;

    uint16_t outline =
        (i == selectedItem)
            ? ST77XX_WHITE
            : ST77XX_CYAN;

    tft.fillRoundRect(
        10, y,
        220, 60,
        10,
        fillColor);

    tft.drawRoundRect(
        10, y,
        220, 60,
        10,
        outline);

    tft.setTextColor(
        products[i].color);

    tft.setTextSize(2);

    tft.setCursor(
        20,
        y + 18);

    tft.print(
        products[i].name);

    y += 75;
  }

  // Bottom information
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  tft.setCursor(10, 280);
  tft.print("Touch an item to continue");

  tft.setTextColor(ST77XX_YELLOW);

  tft.setCursor(10, 300);
  tft.print(statusMessage);
}

// =================================================
// DRAW QR CODE
// =================================================

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

  // 240 is the real screen width
  int x0 =
      (240 - qrPixels) / 2;

  int y0 = 65;

  // White background
  tft.fillRect(
      x0 - 8,
      y0 - 8,
      qrPixels + 16,
      qrPixels + 16,
      ST77XX_WHITE);

  for (uint8_t y = 0;
       y < qrcode.size;
       y++)
  {
    for (uint8_t x = 0;
         x < qrcode.size;
         x++)
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
            ST77XX_BLACK);
      }
    }
  }
}

// =================================================
// PAYMENT SCREEN
// =================================================

void drawPaymentScreen()
{
  tft.fillScreen(ST77XX_BLACK);

  // Header
  tft.fillRect(
      0, 0,
      240, 45,
      ST77XX_BLUE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  tft.setCursor(55, 12);
  tft.print("SCAN TO PAY");

  // QR
  drawQRCode();

  // Amount
  String amount =
      String(currentTotal) +
      " TZS";

  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);

  int16_t x1, y1;
  uint16_t w, h;

  tft.getTextBounds(
      amount,
      0,
      0,
      &x1,
      &y1,
      &w,
      &h);

  tft.setCursor(
      (240 - w) / 2,
      245);

  tft.print(amount);

  // Instruction
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  tft.setCursor(50, 285);
  tft.print("Scan with your phone");

  // Status
  tft.setTextColor(ST77XX_YELLOW);

  tft.setCursor(10, 305);
  tft.print(statusMessage);
}

// =================================================
// STATUS SCREEN
// =================================================

void drawStatusScreen()
{
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(
      0, 0,
      240, 45,
      ST77XX_BLUE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  tft.setCursor(35, 12);
  tft.print("VENDING STATUS");

  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(2);

  tft.setCursor(10, 90);
  tft.print(statusMessage);

  if (currentOrderId.length())
  {
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);

    tft.setCursor(10, 180);
    tft.print("Order:");

    tft.setCursor(60, 180);
    tft.print(currentOrderId);
  }
}

// =================================================
// UPDATE SCREEN
// =================================================

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

// =================================================
// CREATE ORDER
// =================================================

bool createOrder(int itemIndex)
{
  HTTPClient http;

  String url =
      buildBackendUrl("/order");

  Serial.println();
  Serial.println("================================");
  Serial.println("CREATING ORDER");
  Serial.println("URL: " + url);

  http.begin(url);

  http.addHeader(
      "Content-Type",
      "application/json");

  StaticJsonDocument<512> request;

  request["machine_id"] =
      machineId;

  JsonArray items =
      request.createNestedArray("items");

  JsonObject item =
      items.createNestedObject();

  item["product_id"] =
      products[itemIndex].drinkId;

  item["quantity"] = 1;

  String payload;

  serializeJson(
      request,
      payload);

  Serial.println("Request:");
  Serial.println(payload);

  int statusCode =
      http.POST(payload);

  Serial.print("HTTP status: ");
  Serial.println(statusCode);

  if (statusCode != HTTP_CODE_OK)
  {
    Serial.println(
        "Order request failed.");

    http.end();

    return false;
  }

  String response =
      http.getString();

  Serial.println(
      "Backend response:");

  Serial.println(response);

  http.end();

  StaticJsonDocument<4096> doc;

  DeserializationError error =
      deserializeJson(
          doc,
          response);

  if (error)
  {
    Serial.print(
        "JSON parsing error: ");

    Serial.println(
        error.c_str());

    return false;
  }

  String backendStatus =
      doc["status"].as<String>();

  if (backendStatus != "success")
  {
    Serial.println(
        "Backend rejected order.");

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
  Serial.println(
      "ORDER CREATED SUCCESSFULLY");

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

// =================================================
// CONNECT WEBSOCKET
// =================================================

void connectWebSocket()
{
  statusMessage =
      "Connecting backend...";

  updateScreen();

  String wsPath =
      String("/ws/") + machineId;

  Serial.printf(
      "WebSocket connect to ws://%s:%u%s\n",
      backendHost,
      backendPort,
      wsPath.c_str());

  webSocket.begin(
      backendHost,
      backendPort,
      wsPath.c_str());

  webSocket.onEvent(
      [](WStype_t type,
         uint8_t *payload,
         size_t length)
      {
        switch (type)
        {
        case WStype_CONNECTED:
        {
          Serial.println(
              "WebSocket connected");

          statusMessage =
              "Backend connected";

          updateScreen();

          break;
        }

        case WStype_DISCONNECTED:
        {
          Serial.println(
              "WebSocket disconnected");

          statusMessage =
              "Backend disconnected";

          updateScreen();

          break;
        }

        case WStype_TEXT:
        {
          String message;

          for (size_t i = 0;
               i < length;
               i++)
          {
            message +=
                (char)payload[i];
          }

          Serial.println(
              "WebSocket message:");

          Serial.println(message);

          StaticJsonDocument<512> msgDoc;

          DeserializationError err =
              deserializeJson(
                  msgDoc,
                  message);

          if (!err)
          {
            String action =
                msgDoc["action"]
                    .as<String>();

            if (action == "dispense")
            {
              int drinkId =
                  msgDoc["drink_id"]
                      .as<int>();

              String orderId =
                  msgDoc["order_id"]
                      .as<String>();

              statusMessage =
                  "Payment confirmed";

              systemState =
                  STATE_DISPENSING;

              updateScreen();

              String command =
                  "M" +
                  String(drinkId) +
                  "\n";

              Serial2.print(command);

              Serial.println(
                  "Sent to Arduino: " +
                  command);
            }
          }
          else
          {
            Serial.println(
                "WebSocket JSON error");
          }

          break;
        }

        default:
          break;
        }
      });

  webSocket.setReconnectInterval(5000);
}

// =================================================
// ARDUINO RESPONSE
// =================================================

void processArduinoResponse()
{
  if (!Serial2.available())
  {
    return;
  }

  String response =
      Serial2.readStringUntil('\n');

  response.trim();

  if (response.length() == 0)
  {
    return;
  }

  Serial.println(
      "Arduino response: " +
      response);

  if (response == "DONE")
  {
    statusMessage =
        "Item dispensed. Thank you!";

    systemState =
        STATE_DONE;
  }
  else if (response == "EMPTY")
  {
    statusMessage =
        "Item unavailable.";

    systemState =
        STATE_ERROR;
  }
  else if (response == "FAILED")
  {
    statusMessage =
        "Dispense failed.";

    systemState =
        STATE_ERROR;
  }
  else
  {
    statusMessage =
        "Arduino: " +
        response;

    systemState =
        STATE_ERROR;
  }

  updateScreen();
}

// =================================================
// WIFI
// =================================================

void connectWiFi()
{
  WiFi.mode(WIFI_STA);

  WiFi.begin(
      ssid,
      password);

  unsigned long start =
      millis();

  while (
      WiFi.status() != WL_CONNECTED &&
      millis() - start < 20000)
  {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    statusMessage =
        "Connected to Wi-Fi";

    Serial.print(
        "Wi-Fi connected, IP = ");

    Serial.println(
        WiFi.localIP());
  }
  else
  {
    statusMessage =
        "Wi-Fi failed";

    Serial.println(
        "Wi-Fi connection failed");
  }

  updateScreen();
}

// =================================================
// SETUP
// =================================================

void setup()
{
  Serial.begin(115200);

  // ESP32 <-> Arduino
  Serial2.begin(
      9600,
      SERIAL_8N1,
      16,
      17);

  // Start TFT SPI using your confirmed working pins
  SPI.begin(
      TFT_CLK,
      TFT_MISO,
      TFT_MOSI,
      TFT_CS);

  // Start ST7789
  tft.init(240, 320);

  // Confirmed working orientation
  tft.setRotation(2);

  tft.fillScreen(ST77XX_BLACK);

  setTouchPins();

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED)
  {
    connectWebSocket();
  }

  updateScreen();
}

// =================================================
// LOOP
// =================================================

void loop()
{
  webSocket.loop();

  processArduinoResponse();

  // Reconnect Wi-Fi
  if (
      WiFi.status() != WL_CONNECTED &&
      millis() - lastWsReconnect > 10000)
  {
    lastWsReconnect =
        millis();

    connectWiFi();

    if (
        WiFi.status() == WL_CONNECTED)
    {
      connectWebSocket();
    }
  }

  // Touch handling
  if (
      systemState == STATE_MENU &&
      millis() - lastTouchCheck > 50)
  {
    lastTouchCheck =
        millis();

    int touchX;
    int touchY;

    bool touchDown =
        readTouchPoint(
            touchX,
            touchY);

    // Only one action per finger press
    if (
        touchDown &&
        !touchWasDown)
    {
      int item =
          getTouchedItem(
              touchX,
              touchY);

      if (item >= 0)
      {
        selectedItem = item;

        statusMessage =
            String("Selected: ") +
            products[item].name;

        updateScreen();

        if (createOrder(item))
        {
          systemState =
              STATE_WAITING_PAYMENT;

          statusMessage =
              "Scan QR to pay";
        }
        else
        {
          systemState =
              STATE_ERROR;

          statusMessage =
              "Unable to create order.";
        }

        updateScreen();
      }
    }

    touchWasDown =
        touchDown;
  }
}