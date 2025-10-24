#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Hash.h>

// Настройки точки доступа
const char* ssid = "MPU6050_Sensor_Network";
const char* password = "12345678";

// Настройки UDP
WiFiUDP udp;
const unsigned int udpPort = 8888;

// Настройки WebSocket
#include <WebSocketsServer.h>
WebSocketsServer webSocket = WebSocketsServer(81);

// Структура для хранения данных датчиков
struct SensorData {
  String name;
  double pitch;
  double roll;
  double yaw;
  double abs_pitch;
  double abs_roll;
  double abs_yaw;
  unsigned long timestamp;
  unsigned long lastUpdate;
  bool active;
};

// Хранилище данных датчиков
const int MAX_SENSORS = 20; // Увеличим лимит датчиков
SensorData sensors[MAX_SENSORS];
int sensorCount = 0;

// Буферы для работы с данными
char packetBuffer[512];
DynamicJsonDocument jsonDoc(1024);

// Статистика
unsigned long packetsReceived = 0;
unsigned long packetsError = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting MPU6050 Sensor Server...");
  
  // Инициализация массива датчиков
  for (int i = 0; i < MAX_SENSORS; i++) {
    sensors[i].active = false;
  }
  
  // Запуск точки доступа
  WiFi.softAP(ssid, password);
  Serial.print("Access Point Started. IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  // Запуск UDP сервера
  udp.begin(udpPort);
  Serial.print("UDP Server started on port ");
  Serial.println(udpPort);
  
  // Запуск WebSocket сервера
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket Server started on port 81");
  
  Serial.println("Server ready! Waiting for sensor connections...");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        
        // Отправляем JSON приветствие вместо текста
        DynamicJsonDocument doc(200);
        doc["type"] = "connection";
        doc["status"] = "connected";
        doc["message"] = "Connected to MPU6050 Sensor Server";
        doc["sensor_count"] = getActiveSensorCount();
        doc["protocol_version"] = "1.0";
        
        String jsonString;
        serializeJson(doc, jsonString);
        webSocket.sendTXT(num, jsonString);
        
        // Отправляем текущий список датчиков новому клиенту
        sendSensorListToClient(num);
      }
      break;
    case WStype_TEXT:
      // Обработка текстовых сообщений от клиентов
      Serial.printf("[%u] Received text: %s\n", num, payload);
      break;
  }
}

// Поиск или создание записи датчика
int findOrCreateSensor(const String& name) {
  // Поиск существующего датчика
  for (int i = 0; i < sensorCount; i++) {
    if (sensors[i].name == name) {
      return i;
    }
  }
  
  // Создание нового датчика, если есть место
  if (sensorCount < MAX_SENSORS) {
    sensors[sensorCount].name = name;
    sensors[sensorCount].lastUpdate = millis();
    sensors[sensorCount].active = true;
    Serial.println("New sensor registered: " + name);
    return sensorCount++;
  }
  
  return -1; // Нет места для нового датчика
}

// Обработка входящих UDP пакетов
void processUDPPacket() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';
      
      packetsReceived++;
      
      // Логирование для отладки (только каждые 100 пакетов)
      if (packetsReceived % 100 == 0) {
        Serial.printf("Received packet #%d from %s:%d\n", 
                     packetsReceived, 
                     udp.remoteIP().toString().c_str(), 
                     udp.remotePort());
      }
      
      // Парсинг JSON данных
      DeserializationError error = deserializeJson(jsonDoc, packetBuffer);
      if (!error) {
        // Проверяем обязательные поля
        if (!jsonDoc["sensor"].is<String>()) {
          Serial.println("Error: Missing sensor name field");
          packetsError++;
          return;
        }
        
        String sensorName = jsonDoc["sensor"].as<String>();
        
        int sensorIndex = findOrCreateSensor(sensorName);
        if (sensorIndex != -1) {
          // Обновление данных датчика с проверкой наличия полей
          sensors[sensorIndex].pitch = jsonDoc["pitch"] | 0.0;
          sensors[sensorIndex].roll = jsonDoc["roll"] | 0.0;
          sensors[sensorIndex].yaw = jsonDoc["yaw"] | 0.0;
          sensors[sensorIndex].abs_pitch = jsonDoc["abs_pitch"] | 0.0;
          sensors[sensorIndex].abs_roll = jsonDoc["abs_roll"] | 0.0;
          sensors[sensorIndex].abs_yaw = jsonDoc["abs_yaw"] | 0.0;
          sensors[sensorIndex].timestamp = jsonDoc["timestamp"] | millis();
          sensors[sensorIndex].lastUpdate = millis();
          sensors[sensorIndex].active = true;
          
          // Отправка данных всем WebSocket клиентам
          broadcastSensorData(sensorIndex);
        } else {
          Serial.println("Error: Maximum sensor limit reached");
          packetsError++;
        }
      } else {
        Serial.print("JSON parsing error: ");
        Serial.println(error.c_str());
        packetsError++;
        
        // Вывод сырых данных для отладки
        Serial.print("Raw data: ");
        Serial.println(packetBuffer);
      }
    }
  }
}

// Рассылка данных датчика всем WebSocket клиентам
void broadcastSensorData(int sensorIndex) {
  if (webSocket.connectedClients() == 0) return; // Нет подключенных клиентов
  
  DynamicJsonDocument doc(512);
  doc["type"] = "sensor_data";
  doc["sensor"] = sensors[sensorIndex].name;
  doc["pitch"] = sensors[sensorIndex].pitch;
  doc["roll"] = sensors[sensorIndex].roll;
  doc["yaw"] = sensors[sensorIndex].yaw;
  doc["abs_pitch"] = sensors[sensorIndex].abs_pitch;
  doc["abs_roll"] = sensors[sensorIndex].abs_roll;
  doc["abs_yaw"] = sensors[sensorIndex].abs_yaw;
  doc["timestamp"] = sensors[sensorIndex].timestamp;
  doc["server_time"] = millis();
  doc["active_sensors"] = getActiveSensorCount();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  webSocket.broadcastTXT(jsonString);
}

// Отправка списка датчиков конкретному клиенту
void sendSensorListToClient(uint8_t clientNum) {
  DynamicJsonDocument doc(1024);
  doc["type"] = "sensor_list";
  
  JsonArray sensorsArray = doc.createNestedArray("sensors");
  for (int i = 0; i < sensorCount; i++) {
    if (sensors[i].active && (millis() - sensors[i].lastUpdate < 10000)) {
      JsonObject sensorObj = sensorsArray.createNestedObject();
      sensorObj["name"] = sensors[i].name;
      sensorObj["last_update"] = millis() - sensors[i].lastUpdate;
    }
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  webSocket.sendTXT(clientNum, jsonString);
}

// Отправка списка всех датчиков всем клиентам
void broadcastSensorList() {
  if (webSocket.connectedClients() == 0) return;
  
  DynamicJsonDocument doc(1024);
  doc["type"] = "sensor_list";
  
  JsonArray sensorsArray = doc.createNestedArray("sensors");
  for (int i = 0; i < sensorCount; i++) {
    if (sensors[i].active && (millis() - sensors[i].lastUpdate < 10000)) {
      JsonObject sensorObj = sensorsArray.createNestedObject();
      sensorObj["name"] = sensors[i].name;
      sensorObj["last_update"] = millis() - sensors[i].lastUpdate;
    }
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  webSocket.broadcastTXT(jsonString);
}

// Получение количества активных датчиков
int getActiveSensorCount() {
  int count = 0;
  for (int i = 0; i < sensorCount; i++) {
    if (sensors[i].active && (millis() - sensors[i].lastUpdate < 10000)) {
      count++;
    }
  }
  return count;
}

// Очистка неактивных датчиков
void cleanupInactiveSensors() {
  unsigned long currentTime = millis();
  int cleanedCount = 0;
  
  for (int i = 0; i < sensorCount; i++) {
    // Если датчик не обновлялся более 30 секунд, помечаем как неактивный
    if (currentTime - sensors[i].lastUpdate > 30000) {
      if (sensors[i].active) {
        Serial.println("Sensor timeout: " + sensors[i].name);
        sensors[i].active = false;
        cleanedCount++;
      }
    }
  }
  
  if (cleanedCount > 0) {
    Serial.println("Cleaned " + String(cleanedCount) + " inactive sensors");
  }
}

// Компактизация массива датчиков (удаление неактивных)
void compactSensorArray() {
  static unsigned long lastCompact = 0;
  if (millis() - lastCompact > 60000) { // Каждую минуту
    int newCount = 0;
    for (int i = 0; i < sensorCount; i++) {
      if (sensors[i].active) {
        if (i != newCount) {
          sensors[newCount] = sensors[i];
        }
        newCount++;
      }
    }
    
    if (newCount != sensorCount) {
      Serial.println("Compacted sensor array: " + String(sensorCount) + " -> " + String(newCount));
      sensorCount = newCount;
    }
    
    lastCompact = millis();
  }
}

void loop() {
  // Обработка WebSocket событий
  webSocket.loop();
  
  // Обработка UDP пакетов
  processUDPPacket();
  
  // Периодическая рассылка списка датчиков
  static unsigned long lastBroadcast = 0;
  if (millis() - lastBroadcast > 5000) { // Каждые 5 секунд
    broadcastSensorList();
    lastBroadcast = millis();
  }
  
  // Очистка неактивных датчиков
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 10000) { // Каждые 10 секунд
    cleanupInactiveSensors();
    lastCleanup = millis();
  }
  
  // Компактизация массива
  compactSensorArray();
  
  // Вывод статуса в Serial (для отладки)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 15000) {
    Serial.println("=== SERVER STATUS ===");
    Serial.print("Active sensors: ");
    Serial.print(getActiveSensorCount());
    Serial.print("/");
    Serial.print(sensorCount);
    Serial.print(", WebSocket clients: ");
    Serial.println(webSocket.connectedClients());
    Serial.print("Packets received: ");
    Serial.print(packetsReceived);
    Serial.print(", Errors: ");
    Serial.println(packetsError);
    
    if (packetsReceived > 0) {
      float errorRate = (float)packetsError / packetsReceived * 100.0;
      Serial.print("Error rate: ");
      Serial.print(errorRate, 2);
      Serial.println("%");
    }
    
    // Вывод списка активных датчиков
    Serial.println("Active sensors list:");
    for (int i = 0; i < sensorCount; i++) {
      if (sensors[i].active && (millis() - sensors[i].lastUpdate < 10000)) {
        Serial.print("  ");
        Serial.print(sensors[i].name);
        Serial.print(" (last update: ");
        Serial.print(millis() - sensors[i].lastUpdate);
        Serial.println(" ms ago)");
      }
    }
    Serial.println("===================");
    
    lastStatus = millis();
  }
  
  delay(5); // Уменьшаем задержку для большей отзывчивости
}
