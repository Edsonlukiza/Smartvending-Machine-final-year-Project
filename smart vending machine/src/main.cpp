#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);
WebSocketsClient webSocket;

const char *ssid = "YourWiFiSSID";
const char *password = "YourWiFiPassword";
const char *backendHost = "192.168.1.100";
const uint16_t backendPort = 8000;
const char *machineId = "vending01";

struct Product
{
  const char *name;
  int drinkId;
  uint16_t color;
};

Product products[] = {
    {"Cola", 1, ILI9341_RED},
    {"Lemon Tea", 2, ILI9341_YELLOW},
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
int qrSize = 0;
uint8_t qrData[1024];
unsigned long lastTouchCheck = 0;
unsigned long lastWsReconnect = 0;

String buildBackendUrl(const char *path)
{
  return String("http://") + backendHost + ":" + String(backendPort) + path;
}

void setTouchPins()
{
  pinMode(TOUCH_CS, OUTPUT);
  pinMode(TOUCH_CLK, OUTPUT);
  pinMode(TOUCH_DIN, OUTPUT);
  pinMode(TOUCH_DO, INPUT);
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

void drawPaymentScreen()
{
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 240, 45, ILI9341_BLUE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 12);
  tft.print("Payment QR");

  if (qrSize > 0)
  {
    int pixelSize = 5;
    int qrWidth = qrSize * pixelSize;
    int x0 = (240 - qrWidth) / 2;
    int y0 = 60;
    tft.fillRect(x0 - 6, y0 - 6, qrWidth + 12, qrWidth + 12, ILI9341_WHITE);
    for (int row = 0; row < qrSize; row++)
    {
      for (int col = 0; col < qrSize; col++)
      {
        bool dark = qrData[row * qrSize + col];
        uint16_t color = dark ? ILI9341_BLACK : ILI9341_WHITE;
        tft.fillRect(x0 + col * pixelSize, y0 + row * pixelSize, pixelSize, pixelSize, color);
      }
    }
  }
  else
  {
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(1);
    tft.setCursor(10, 80);
    tft.print("Unable to draw QR code.");
  }

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 250);
  tft.print("Order ID: ");
  tft.setCursor(10, 265);
  tft.print(currentOrderId);

  tft.setCursor(10, 285);
  tft.print("Open URL if QR fails:");
  tft.setCursor(10, 300);
  tft.print(currentPaymentUrl);
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
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> request;
  request["machine_id"] = machineId;
  request["drink_id"] = products[itemIndex].drinkId;
  String payload;
  serializeJson(request, payload);

  int statusCode = http.POST(payload);
  if (statusCode != HTTP_CODE_OK)
  {
    http.end();
    return false;
  }

  String response = http.getString();
  http.end();

  StaticJsonDocument<8192> doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error)
  {
    return false;
  }

  if (String(doc["status"].as<const char *>()) != "success")
  {
    return false;
  }

  currentOrderId = String(doc["order_id"].as<const char *>());
  currentPaymentUrl = String(doc["payment_url"].as<const char *>());
  qrSize = doc["qr_size"].as<int>();
  JsonArray qrArray = doc["qr_data"].as<JsonArray>();
  int total = min((int)qrArray.size(), (int)sizeof(qrData));
  for (int i = 0; i < total; i++)
  {
    qrData[i] = qrArray[i].as<int>();
  }

  return true;
}

void connectWebSocket()
{
  String wsPath = String("/ws/") + machineId;
  webSocket.begin(backendHost, backendPort, wsPath.c_str());
  webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                    {
    switch (type) {
      case WStype_CONNECTED:
        statusMessage = "Backend connected";
        updateScreen();
        break;
      case WStype_DISCONNECTED:
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
  }
  else
  {
    statusMessage = "Wi-Fi failed";
  }
  updateScreen();
}

void setup()
{
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  tft.begin();
  tft.setRotation(1);
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

  if (systemState == STATE_MENU && millis() - lastTouchCheck > 150)
  {
    lastTouchCheck = millis();
    uint16_t rawX = readTouchAxis(0x90);
    uint16_t rawY = readTouchAxis(0xD0);

    if (rawX > 200 && rawX < 3900 && rawY > 200 && rawY < 3900)
    {
      int touchX = map(rawY, 200, 3900, 240, 0);
      int touchY = map(rawX, 200, 3900, 0, 320);
      int item = getTouchedItem(touchX, touchY);
      if (item >= 0)
      {
        selectedItem = item;
        statusMessage = String("Selected: ") + products[item].name;
        updateScreen();
        delay(150);

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
  }
}
