#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>

Adafruit_MPU6050 mpu;

// WiFi credentials для подключения к AP
const char* ssid = "ESP8266_AP";
const char* password = "12345678";

// Адрес WebSocket сервера
const char* websocket_server = "192.168.4.1";
const uint16_t websocket_port = 81;

// Sensor data
float pitch = 0, roll = 0, yaw = 0;
float lastSentPitch = 0, lastSentRoll = 0, lastSentYaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;

// Относительный ноль - ИЗМЕНЕНО: теперь это смещение от текущего положения
float zeroPitchOffset = 0, zeroRollOffset = 0, zeroYawOffset = 0;
bool zeroSet = false;

// Накопленные углы (без ограничений) - ИЗМЕНЕНО: теперь для всех осей
double accumulatedPitch = 0, accumulatedRoll = 0, accumulatedYaw = 0;
float prevPitch = 0, prevRoll = 0, prevYaw = 0;
bool firstMeasurement = true;

// WebSocket client
WebSocketsClient webSocket;
bool wsConnected = false;
unsigned long lastConnectionAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000; // 5 секунд

// Настройки отправки данных
const unsigned long SEND_INTERVAL = 50;
const float CHANGE_THRESHOLD = 1.0;

// Уникальный ID устройства
String deviceId = "VR_Head_" + String(ESP.getChipId());

// Временная строка для отправки сообщений
String tempMessage = "";

// НОВАЯ ФУНКЦИЯ: Обнуление всех углов
void resetAllAngles() {
  pitch = 0;
  roll = 0;
  yaw = 0;
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  prevPitch = 0;
  prevRoll = 0;
  prevYaw = 0;
  firstMeasurement = true;
  
  Serial.println("All angles reset to zero");
  if (wsConnected) {
    tempMessage = "ANGLES_RESET:" + deviceId; // ИСПРАВЛЕНИЕ: создаем переменную
    webSocket.sendTXT(tempMessage);
  }
}

// НОВАЯ ФУНКЦИЯ: Установка текущего положения как нулевой точки
void setCurrentPositionAsZero() {
  // Сохраняем текущие абсолютные углы как смещение нуля
  zeroPitchOffset = accumulatedPitch;
  zeroRollOffset = accumulatedRoll;
  zeroYawOffset = accumulatedYaw;
  zeroSet = true;
  
  Serial.println("Current position set as zero point");
  Serial.print("Zero Pitch Offset: "); Serial.print(zeroPitchOffset, 2);
  Serial.print(" Roll Offset: "); Serial.print(zeroRollOffset, 2);
  Serial.print(" Yaw Offset: "); Serial.println(zeroYawOffset, 2);
  
  if (wsConnected) {
    tempMessage = "ZERO_SET_AT_CURRENT:PITCH:" + String(zeroPitchOffset, 2) + 
                     ",ROLL:" + String(zeroRollOffset, 2) + 
                     ",YAW:" + String(zeroYawOffset, 2); // ИСПРАВЛЕНИЕ: используем tempMessage
    webSocket.sendTXT(tempMessage);
  }
}

// Сброс относительного нуля
void resetZeroPoint() {
  zeroPitchOffset = 0;
  zeroRollOffset = 0;
  zeroYawOffset = 0;
  zeroSet = false;
  
  Serial.println("Zero point reset");
  if (wsConnected) {
    tempMessage = "ZERO_RESET"; // ИСПРАВЛЕНИЕ: используем tempMessage
    webSocket.sendTXT(tempMessage);
  }
}

// Расчет накопленных углов (без ограничений) - ИЗМЕНЕНО: для всех осей
void updateAccumulatedAngles() {
  if (firstMeasurement) {
    prevPitch = pitch;
    prevRoll = roll;
    prevYaw = yaw;
    firstMeasurement = false;
    return;
  }
  
  // Вычисляем разницу углов с учетом переходов через 180/-180 для ВСЕХ осей
  float deltaPitch = pitch - prevPitch;
  float deltaRoll = roll - prevRoll;
  float deltaYaw = yaw - prevYaw;
  
  // Корректируем разницу для переходов через границу ±180 для ВСЕХ осей
  if (deltaPitch > 180) deltaPitch -= 360;
  else if (deltaPitch < -180) deltaPitch += 360;
  
  if (deltaRoll > 180) deltaRoll -= 360;
  else if (deltaRoll < -180) deltaRoll += 360;
  
  if (deltaYaw > 180) deltaYaw -= 360;
  else if (deltaYaw < -180) deltaYaw += 360;
  
  // Накопление углов для ВСЕХ осей
  accumulatedPitch += deltaPitch;
  accumulatedRoll += deltaRoll;
  accumulatedYaw += deltaYaw;
  
  prevPitch = pitch;
  prevRoll = roll;
  prevYaw = yaw;
}

// Получение относительных углов (относительно зафиксированного нуля) - ИЗМЕНЕНО
double getRelativePitch() {
  if (!zeroSet) return accumulatedPitch;
  return accumulatedPitch - zeroPitchOffset;
}

double getRelativeRoll() {
  if (!zeroSet) return accumulatedRoll;
  return accumulatedRoll - zeroRollOffset;
}

double getRelativeYaw() {
  if (!zeroSet) return accumulatedYaw;
  return accumulatedYaw - zeroYawOffset;
}

void calibrateSensor() {
  Serial.println("Calibrating...");
  float sumX = 0, sumY = 0, sumZ = 0;
  
  for (int i = 0; i < 500; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    delay(2);
  }
  
  gyroOffsetX = sumX / 500;
  gyroOffsetY = sumY / 500;
  gyroOffsetZ = sumZ / 500;
  calibrated = true;
  
  Serial.println("Calibration complete");
}

void sendSensorData() {
  if (!wsConnected) return;
  
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  tempMessage = "deviceId:" + deviceId + 
                ",PITCH:" + String(pitch, 1) + 
                ",ROLL:" + String(roll, 1) + 
                ",YAW:" + String(yaw, 1) +
                ",REL_PITCH:" + String(relPitch, 2) +
                ",REL_ROLL:" + String(relRoll, 2) +
                ",REL_YAW:" + String(relYaw, 2) +
                ",ACC_PITCH:" + String(accumulatedPitch, 2) +
                ",ACC_ROLL:" + String(accumulatedRoll, 2) +
                ",ACC_YAW:" + String(accumulatedYaw, 2) +
                ",ZERO_SET:" + String(zeroSet ? "true" : "false") +
                ",ZERO_PITCH:" + String(zeroPitchOffset, 2) +
                ",ZERO_ROLL:" + String(zeroRollOffset, 2) +
                ",ZERO_YAW:" + String(zeroYawOffset, 2); // ИСПРАВЛЕНИЕ: используем tempMessage
  
  webSocket.sendTXT(tempMessage);
  lastSentPitch = pitch;
  lastSentRoll = roll;
  lastSentYaw = yaw;
  
  Serial.println("Data sent: " + tempMessage);
}

bool dataChanged() {
  return (abs(pitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(roll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(yaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

// Обработка событий WebSocket
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      wsConnected = false;
      break;
      
    case WStype_CONNECTED:
      {
        Serial.println("WebSocket connected to server");
        wsConnected = true;
        // Отправляем идентификацию при подключении
        tempMessage = "CLIENT_ID:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
        webSocket.sendTXT(tempMessage);
        sendSensorData(); // Отправляем данные сразу после подключения
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("Received from server: %s\n", message);
        
        if (message == "GET_DATA") {
          sendSensorData();
        }
        else if (message == "RECALIBRATE") {
          calibrated = false;
          calibrateSensor();
          tempMessage = "RECALIBRATION_COMPLETE:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
        }
        else if (message == "RESET_ANGLES") {
          resetAllAngles(); // ИЗМЕНЕНО: используем новую функцию
          tempMessage = "ANGLES_RESET:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
          sendSensorData();
        }
        else if (message == "SET_ZERO") {
          setCurrentPositionAsZero();
          tempMessage = "ZERO_POINT_SET:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
        }
        else if (message == "RESET_ZERO") {
          resetZeroPoint();
          tempMessage = "ZERO_POINT_RESET:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
        }
        // Новая команда для принудительной калибровки
        else if (message == "FORCE_CALIBRATE") {
          Serial.println("Force calibration requested");
          calibrated = false;
          calibrateSensor();
          tempMessage = "FORCE_CALIBRATION_COMPLETE:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
        }
        // НОВАЯ КОМАНДА: Установить текущее положение как ноль
        else if (message == "SET_CURRENT_AS_ZERO") {
          Serial.println("Set current position as zero requested");
          setCurrentPositionAsZero();
          tempMessage = "CURRENT_POSITION_SET_AS_ZERO:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
        }
        // НОВАЯ КОМАНДА: Обнулить все углы
        else if (message == "RESET_ALL_ANGLES") {
          Serial.println("Reset all angles requested");
          resetAllAngles();
          tempMessage = "ALL_ANGLES_RESET:" + deviceId; // ИСПРАВЛЕНИЕ: используем tempMessage
          webSocket.sendTXT(tempMessage);
          sendSensorData();
        }
      }
      break;
      
    case WStype_ERROR:
      Serial.println("WebSocket error");
      wsConnected = false;
      break;
  }
}

// Подключение к WiFi
bool connectToWiFi() {
  Serial.println("Attempting to connect to WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    return false;
  }
}

// Подключение к WebSocket серверу
void connectToWebSocket() {
  Serial.println("Connecting to WebSocket server...");
  webSocket.begin(websocket_server, websocket_port, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
}

// Проверка и восстановление подключения
void checkAndReconnect() {
  unsigned long currentTime = millis();
  
  // Проверяем WiFi соединение
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost!");
    wsConnected = false;
    connectToWiFi();
  }
  
  // Пытаемся переподключиться к WebSocket если соединение потеряно
  if (!wsConnected && currentTime - lastConnectionAttempt >= RECONNECT_INTERVAL) {
    Serial.println("Attempting to reconnect WebSocket...");
    connectToWebSocket();
    lastConnectionAttempt = currentTime;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("VR MPU6050 Client Starting...");
  Serial.println("Device ID: " + deviceId);
  
  // Инициализация I2C и MPU6050
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1);
  }
  
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
  
  // Подключение к WiFi
  WiFi.mode(WIFI_STA);
  connectToWiFi();
  
  // Подключение к WebSocket серверу
  if (WiFi.status() == WL_CONNECTED) {
    connectToWebSocket();
  }
  
  // Калибровка сенсора
  calibrateSensor();
  
  lastConnectionAttempt = millis();
  
  Serial.println("VR MPU6050 Client started");
  Serial.println("Ready to send sensor data to server");
  Serial.println("Available commands from server:");
  Serial.println("  - SET_ZERO: Set current position as zero point");
  Serial.println("  - SET_CURRENT_AS_ZERO: Set current position as zero point (alternative)");
  Serial.println("  - RESET_ZERO: Reset zero point");
  Serial.println("  - RESET_ALL_ANGLES: Reset all angles to zero"); // ДОБАВЛЕНО
  Serial.println("  - FORCE_CALIBRATE: Force sensor recalibration");
  Serial.println("  - RECALIBRATE: Recalibrate sensor");
  Serial.println("  - RESET_ANGLES: Reset all angles");
  Serial.println("  - GET_DATA: Request current sensor data");
}

void loop() {
  // Проверка и восстановление подключений
  checkAndReconnect();
  
  // Обработка WebSocket
  webSocket.loop();
  
  if (!calibrated) return;
  
  // Чтение данных с MPU6050
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) deltaTime = 0.01;
  lastTime = currentTime;
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  float accelPitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float accelRoll = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  
  // Обновление углов с комплементарным фильтром
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  float alpha = 0.96;
  pitch = alpha * pitch + (1.0 - alpha) * accelPitch;
  roll = alpha * roll + (1.0 - alpha) * accelRoll;
  
  // Ограничение углов для отображения в диапазоне -180 до 180 градусов
  // Это нужно только для визуального отображения, накопленные углы бесконечны
  if (pitch > 180) pitch -= 360;
  else if (pitch < -180) pitch += 360;
  
  if (roll > 180) roll -= 360;
  else if (roll < -180) roll += 360;
  
  if (yaw > 180) yaw -= 360;
  else if (yaw < -180) yaw += 360;
  
  // Отправка данных с интервалом (только если подключены)
  if (wsConnected) {
    static unsigned long lastSend = 0;
    
    if (currentTime - lastSend >= SEND_INTERVAL) {
      if (dataChanged() || lastSend == 0) {
        sendSensorData();
        lastSend = currentTime;
      }
    }
  } else {
    // Если не подключены, выводим статус каждые 10 секунд
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 10000) {
      Serial.println("Waiting for connection to server...");
      Serial.print("WiFi: ");
      Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      Serial.print("WebSocket: ");
      Serial.println(wsConnected ? "Connected" : "Disconnected");
      
      // Выводим текущие накопленные углы для отладки
      Serial.print("Accumulated Angles - Pitch: ");
      Serial.print(accumulatedPitch, 2);
      Serial.print("°, Roll: ");
      Serial.print(accumulatedRoll, 2);
      Serial.print("°, Yaw: ");
      Serial.print(accumulatedYaw, 2);
      Serial.println("°");
      
      lastStatus = millis();
    }
  }
  
  delay(10);
}
