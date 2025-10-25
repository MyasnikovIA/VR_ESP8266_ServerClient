#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>
#include <EEPROM.h>

Adafruit_MPU6050 mpu;

// WiFi credentials
const char* ssid = "ESP8266_Network_Monitor";
const char* password = "12345678";

// Multi-user socket server settings
String SOCKET_SERVER_HOST = "192.168.4.1";
const uint16_t SOCKET_SERVER_PORT = 81;
bool socketServerConnected = false;
unsigned long lastSocketReconnectAttempt = 0;
const unsigned long SOCKET_RECONNECT_INTERVAL = 5000;
WebSocketsClient socketClient;

// Имя подключаемого устройства (будет загружено из EEPROM)
String deviceName = "VR-Head-Hom-001";

// Настройки EEPROM
#define EEPROM_SIZE 512
#define DEVICE_NAME_ADDR 0
#define MAX_DEVICE_NAME_LENGTH 32

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Sensor data
float pitch = 0, roll = 0, yaw = 0;
float lastSentPitch = 0, lastSentRoll = 0, lastSentYaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;

// Относительный ноль
float zeroPitchOffset = 0, zeroRollOffset = 0, zeroYawOffset = 0;
bool zeroSet = false;

// Накопленные углы (без ограничений)
double accumulatedPitch = 0, accumulatedRoll = 0, accumulatedYaw = 0;
float prevPitch = 0, prevRoll = 0, prevYaw = 0;
bool firstMeasurement = true;

// WebSocket connection management
bool clientConnected = false;
unsigned long lastDataSend = 0;
const unsigned long SEND_INTERVAL = 50;
const float CHANGE_THRESHOLD = 1.0;

// Интервал отправки данных на сокет-сервер
unsigned long lastSocketSend = 0;
const unsigned long SOCKET_SEND_INTERVAL = 100;

// Функция для проверки валидности имени устройства
bool isValidDeviceName(const String& name) {
  if (name.length() == 0 || name.length() > MAX_DEVICE_NAME_LENGTH - 1) {
    return false;
  }
  
  // Проверяем на допустимые символы
  for (unsigned int i = 0; i < name.length(); i++) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
          (c >= '0' && c <= '9') || c == '-' || c == '_')) {
      return false;
    }
  }
  
  return true;
}
// Функция для получения IP адреса основного шлюза
String getGatewayIP() {
  IPAddress gateway = WiFi.gatewayIP();
  return gateway.toString();
}

// Функция для очистки имени устройства
String cleanDeviceName(const String& name) {
  String cleanName = "";
  for (unsigned int i = 0; i < name.length(); i++) {
    char c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      cleanName += c;
    } else if (c == ' ') {
      cleanName += '-'; // Заменяем пробелы на дефисы
    }
    // Остальные символы игнорируем
  }
  
  // Если после очистки имя пустое, используем имя по умолчанию
  if (cleanName.length() == 0) {
    cleanName = "VR-Head-Hom-001";
  }
  
  return cleanName;
}

// Функция для сохранения имени устройства в EEPROM
bool saveDeviceNameToEEPROM(const String& name) {
  EEPROM.begin(EEPROM_SIZE);
  delay(10);
  
  // Очищаем область имени
  for (int i = 0; i < MAX_DEVICE_NAME_LENGTH; i++) {
    EEPROM.write(DEVICE_NAME_ADDR + i, 0);
  }
  
  // Сохраняем новое имя
  String cleanName = cleanDeviceName(name);
  unsigned int length = cleanName.length();
  
  for (unsigned int i = 0; i < length && i < MAX_DEVICE_NAME_LENGTH - 1; i++) {
    EEPROM.write(DEVICE_NAME_ADDR + i, cleanName[i]);
  }
  
  // Добавляем нулевой терминатор
  EEPROM.write(DEVICE_NAME_ADDR + length, 0);
  
  bool success = EEPROM.commit();
  delay(10);
  EEPROM.end();
  
  if (success) {
    Serial.println("Device name saved to EEPROM: " + cleanName);
  } else {
    Serial.println("Error saving device name to EEPROM");
  }
  
  return success;
}

// Функция для загрузки имени устройства из EEPROM
String loadDeviceNameFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  delay(10);
  
  char nameBuffer[MAX_DEVICE_NAME_LENGTH + 1];
  bool isValidName = false;
  int nameLength = 0;
  
  // Читаем имя из EEPROM
  for (int i = 0; i < MAX_DEVICE_NAME_LENGTH; i++) {
    char c = EEPROM.read(DEVICE_NAME_ADDR + i);
    
    if (c == 0) {
      // Найден нулевой терминатор
      nameBuffer[i] = 0;
      if (i > 0) {
        isValidName = true;
        nameLength = i;
      }
      break;
    }
    
    // Проверяем на допустимые символы
    if (c < 32 || c > 126) {
      // Недопустимый символ
      nameBuffer[i] = 0;
      break;
    }
    
    nameBuffer[i] = c;
    nameLength = i + 1;
    
    if (i == MAX_DEVICE_NAME_LENGTH - 1) {
      nameBuffer[MAX_DEVICE_NAME_LENGTH] = 0;
    }
  }
  
  EEPROM.end();
  
  String name = String(nameBuffer);
  
  // Проверяем валидность имени
  if (!isValidName || name.length() == 0 || name.length() > MAX_DEVICE_NAME_LENGTH - 1) {
    Serial.println("Invalid device name in EEPROM, using default");
    name = "VR-Head-Hom-001";
    saveDeviceNameToEEPROM(name); // Сохраняем корректное имя по умолчанию
  } else {
    // Проверяем содержимое имени
    bool hasValidChars = false;
    for (int i = 0; i < nameLength; i++) {
      char c = name[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
          (c >= '0' && c <= '9') || c == '-' || c == '_') {
        hasValidChars = true;
        break;
      }
    }
    
    if (!hasValidChars) {
      Serial.println("No valid characters in device name, using default");
      name = "VR-Head-Hom-001";
      saveDeviceNameToEEPROM(name);
    }
  }
  
  String cleanName = cleanDeviceName(name);
  Serial.println("Device name loaded from EEPROM: " + cleanName);
  return cleanName;
}

// Функция для изменения имени устройства
void changeDeviceName(const String& newName) {
  if (newName.length() == 0 || newName.length() > MAX_DEVICE_NAME_LENGTH - 1) {
    Serial.println("Error: Device name must be 1-31 characters long");
    return;
  }
  
  String cleanName = cleanDeviceName(newName);
  if (cleanName.length() == 0) {
    Serial.println("Error: Device name contains only invalid characters");
    return;
  }
  
  String oldName = deviceName;
  deviceName = cleanName;
  
  // Сохраняем в EEPROM
  if (saveDeviceNameToEEPROM(deviceName)) {
    // Устанавливаем новое имя хоста
    WiFi.hostname(deviceName.c_str());
    
    Serial.println("Device name changed from '" + oldName + "' to '" + deviceName + "'");
    
    // Уведомляем всех подключенных клиентов
    String message = "DEVICE_NAME_CHANGED:" + deviceName;
    webSocket.broadcastTXT(message);
    
    // Отправляем обновленное имя на сокет-сервер
    if (socketServerConnected) {
      String socketMsg = "DEVICE_NAME:" + deviceName;
      socketClient.sendTXT(socketMsg);
    }
  } else {
    // В случае ошибки восстанавливаем старое имя
    deviceName = oldName;
    Serial.println("Error: Failed to save device name to EEPROM");
  }
}

// Функция для сброса имени устройства к значению по умолчанию
void resetDeviceName() {
  changeDeviceName("VR-Head-Hom-001");
}

// Функция для обнуления всех углов
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
}

// Установка текущего положения как нулевой точки
void setZeroPoint() {
  zeroPitchOffset = accumulatedPitch;
  zeroRollOffset = accumulatedRoll;
  zeroYawOffset = accumulatedYaw;
  zeroSet = true;
  
  Serial.println("Current position set as zero point");
  Serial.print("Zero Pitch Offset: "); Serial.print(zeroPitchOffset, 2);
  Serial.print(" Roll Offset: "); Serial.print(zeroRollOffset, 2);
  Serial.print(" Yaw Offset: "); Serial.println(zeroYawOffset, 2);
  
  String message = "ZERO_SET:PITCH:" + String(zeroPitchOffset, 2) + 
                   ",ROLL:" + String(zeroRollOffset, 2) + 
                   ",YAW:" + String(zeroYawOffset, 2);
  webSocket.broadcastTXT(message);
}

// Сброс относительного нуля
void resetZeroPoint() {
  zeroPitchOffset = 0;
  zeroRollOffset = 0;
  zeroYawOffset = 0;
  zeroSet = false;
  
  Serial.println("Zero point reset");
  webSocket.broadcastTXT("ZERO_RESET");
}

// Расчет накопленных углов (без ограничений)
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
  
  // Накопление углов для ВСЕХ осей - теперь все оси накапливаются бесконечно
  accumulatedPitch += deltaPitch;
  accumulatedRoll += deltaRoll;
  accumulatedYaw += deltaYaw;
  
  prevPitch = pitch;
  prevRoll = roll;
  prevYaw = yaw;
}

// Получение относительных углов (относительно зафиксированного нуля)
double getRelativePitch() {
  if (!zeroSet) return accumulatedPitch;
  double relative = accumulatedPitch - zeroPitchOffset;
  return relative;
}

double getRelativeRoll() {
  if (!zeroSet) return accumulatedRoll;
  double relative = accumulatedRoll - zeroRollOffset;
  return relative;
}

double getRelativeYaw() {
  if (!zeroSet) return accumulatedYaw;
  double relative = accumulatedYaw - zeroYawOffset;
  return relative;
}

// Функция для визуализации угла в виде прогресс-бара (для отладки)
String visualizeAngle(double angle, int length = 20) {
  String bar = "[";
  int center = length / 2;
  
  // Нормализуем угол к диапазону -180..180 для визуализации
  double normalizedAngle = fmod(angle, 360.0);
  if (normalizedAngle > 180) normalizedAngle -= 360;
  else if (normalizedAngle < -180) normalizedAngle += 360;
  
  // Позиция маркера на шкале
  int markerPos = map(normalizedAngle, -180, 180, 0, length - 1);
  markerPos = constrain(markerPos, 0, length - 1);
  
  for (int i = 0; i < length; i++) {
    if (i == center) {
      bar += "|";  // Центральная метка
    } else if (i == markerPos) {
      bar += "o";  // Маркер текущего угла
    } else {
      bar += " ";
    }
  }
  bar += "]";
  return bar;
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
  Serial.print("Gyro offsets - X: "); Serial.print(gyroOffsetX, 6);
  Serial.print(" Y: "); Serial.print(gyroOffsetY, 6);
  Serial.print(" Z: "); Serial.println(gyroOffsetZ, 6);
}

// Отправка данных на многопользовательский сокет-сервер
void sendDataToSocketServer() {
  if (!socketServerConnected) return;
  
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  // Формируем данные для отправки на сокет-сервер
  String data = "DEVICE:" + deviceName +
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
                ",TIMESTAMP:" + String(millis());
  
  socketClient.sendTXT(data);
}

void sendSensorData() {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  String data = "DEVICE:" + deviceName +
                ",PITCH:" + String(pitch, 1) + 
                ",ROLL:" + String(roll, 1) + 
                ",YAW:" + String(yaw, 1) +
                ",REL_PITCH:" + String(relPitch, 2) +
                ",REL_ROLL:" + String(relRoll, 2) +
                ",REL_YAW:" + String(relYaw, 2) +
                ",ACC_PITCH:" + String(accumulatedPitch, 2) +
                ",ACC_ROLL:" + String(accumulatedRoll, 2) +
                ",ACC_YAW:" + String(accumulatedYaw, 2) +
                ",ZERO_SET:" + String(zeroSet ? "true" : "false");
  
  webSocket.broadcastTXT(data);
  lastSentPitch = pitch;
  lastSentRoll = roll;
  lastSentYaw = yaw;
  
  // Отладочный вывод в Serial
  if (Serial) {
    static unsigned long lastDebugPrint = 0;
    if (millis() - lastDebugPrint > 1000) {
      Serial.println("=== SENSOR DATA ===");
      Serial.println("RELATIVE ANGLES (Unlimited):");
      Serial.print("Pitch: "); Serial.print(relPitch, 2); Serial.print("°  ");
      Serial.println(visualizeAngle(relPitch));
      Serial.print("Roll:  "); Serial.print(relRoll, 2); Serial.print("°  ");
      Serial.println(visualizeAngle(relRoll));
      Serial.print("Yaw:   "); Serial.print(relYaw, 2); Serial.print("°  ");
      Serial.println(visualizeAngle(relYaw));
      
      Serial.println("ABSOLUTE ANGLES:");
      Serial.print("Pitch: "); Serial.print(accumulatedPitch, 2); Serial.print("°  ");
      Serial.println(visualizeAngle(accumulatedPitch));
      Serial.print("Roll:  "); Serial.print(accumulatedRoll, 2); Serial.print("°  ");
      Serial.println(visualizeAngle(accumulatedRoll));
      Serial.print("Yaw:   "); Serial.print(accumulatedYaw, 2); Serial.print("°  ");
      Serial.println(visualizeAngle(accumulatedYaw));
      
      Serial.print("Socket Server: ");
      Serial.println(socketServerConnected ? "CONNECTED" : "DISCONNECTED");
      Serial.println("===================");
      lastDebugPrint = millis();
    }
  }
}

bool dataChanged() {
  return (abs(pitch - lastSentPitch) >= CHANGE_THRESHOLD ||
          abs(roll - lastSentRoll) >= CHANGE_THRESHOLD ||
          abs(yaw - lastSentYaw) >= CHANGE_THRESHOLD);
}

// Обработчик событий сокет-клиента (подключение к многопользовательскому серверу)
void socketClientEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnected from multi-user socket server");
      socketServerConnected = false;
      break;
      
    case WStype_CONNECTED:
      {
        Serial.println("Connected to multi-user socket server!");
        socketServerConnected = true;
        
        // Отправляем информацию об устройстве при подключении
        String connectMsg = "DEVICE_CONNECTED:" + deviceName + ",IP:" + WiFi.localIP().toString();
        socketClient.sendTXT(connectMsg);
        
        Serial.println("Sent device info to socket server: " + connectMsg);
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.println("Received from socket server: " + message);
        
        // Обработка команд от сокет-сервера (если нужно)
        if (message == "PING") {
          socketClient.sendTXT("PONG");
        }
        else if (message == "GET_STATUS") {
          sendDataToSocketServer();
        }
      }
      break;
      
    case WStype_ERROR:
      Serial.println("Socket client error");
      socketServerConnected = false;
      break;
      
    case WStype_PING:
    case WStype_PONG:
      // Поддержание соединения
      break;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      clientConnected = (webSocket.connectedClients() > 0);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        clientConnected = true;
        
        // Отправляем информацию об устройстве при подключении
        String welcomeMsg = "DEVICE_CONNECTED:" + deviceName;
        webSocket.sendTXT(num, welcomeMsg);
        
        sendSensorData();
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[%u] Received: %s\n", num, message);
        
        if (message == "GET_DATA") {
          sendSensorData();
        }
        else if (message == "RECALIBRATE") {
          calibrated = false;
          calibrateSensor();
          String calMessage = "RECALIBRATION_COMPLETE";
          webSocket.broadcastTXT(calMessage);
        }
        else if (message == "RESET_ANGLES" || message == "RA") {
          resetAllAngles();
          String resetMessage = "ANGLES_RESET";
          webSocket.broadcastTXT(resetMessage);
          sendSensorData();
        }
        else if (message == "SET_ZERO" || message == "SZ") {
          setZeroPoint();
          webSocket.broadcastTXT("ZERO_POINT_SET");
        }
        else if (message == "RESET_ZERO" || message == "RZ") {
          resetZeroPoint();
          webSocket.broadcastTXT("ZERO_POINT_RESET");
        }
        else if (message == "GET_DEVICE_INFO") {
          String deviceInfo = "DEVICE_INFO:NAME:" + deviceName + 
                             ",IP:" + WiFi.localIP().toString() +
                             ",MAC:" + WiFi.macAddress() +
                             ",FIRMWARE:VR_Headset_v1.0" +
                             ",UNLIMITED_ANGLES:true" +
                             ",EEPROM:enabled" +
                             ",SOCKET_SERVER:" + String(socketServerConnected ? "connected" : "disconnected");
          webSocket.sendTXT(num, deviceInfo);
        }
        else if (message == "PRINT_DEBUG") {
          // Принудительный вывод отладочной информации
          updateAccumulatedAngles();
          Serial.println("=== DEBUG INFO ===");
          Serial.print("Relative Pitch: "); Serial.println(getRelativePitch(), 2);
          Serial.print("Relative Roll: "); Serial.println(getRelativeRoll(), 2);
          Serial.print("Relative Yaw: "); Serial.println(getRelativeYaw(), 2);
          Serial.print("Accumulated Pitch: "); Serial.println(accumulatedPitch, 2);
          Serial.print("Accumulated Roll: "); Serial.println(accumulatedRoll, 2);
          Serial.print("Accumulated Yaw: "); Serial.println(accumulatedYaw, 2);
          Serial.print("Zero Set: "); Serial.println(zeroSet ? "YES" : "NO");
          Serial.print("Device Name: "); Serial.println(deviceName);
          Serial.print("Socket Server: "); Serial.println(socketServerConnected ? "CONNECTED" : "DISCONNECTED");
          Serial.println("==================");
        }
        else if (message.startsWith("SET_DEVICE_NAME:")) {
          // Команда для изменения имени устройства: SET_DEVICE_NAME:NewName
          String newName = message.substring(16);
          newName.trim();
          if (newName.length() > 0) {
            changeDeviceName(newName);
            String nameChangedMsg = "DEVICE_NAME_CHANGED:" + deviceName;
            webSocket.broadcastTXT(nameChangedMsg);
          } else {
            String errorMsg = "ERROR: Device name cannot be empty";
            webSocket.sendTXT(num, errorMsg);
          }
        }
        else if (message == "RESET_DEVICE_NAME") {
          // Команда для сброса имени устройства к значению по умолчанию
          resetDeviceName();
          String nameResetMsg = "DEVICE_NAME_RESET:" + deviceName;
          webSocket.broadcastTXT(nameResetMsg);
        }
        else if (message == "GET_DEVICE_NAME") {
          // Команда для получения текущего имени устройства
          String currentNameMsg = "CURRENT_DEVICE_NAME:" + deviceName;
          webSocket.sendTXT(num, currentNameMsg);
        }
        else if (message == "HELP" || message == "?") {
          // Команда для вывода справки
          String help = "Available commands:\n"
                       "GET_DATA - Get sensor data\n"
                       "RECALIBRATE - Recalibrate sensor\n"
                       "RESET_ANGLES/RA - Reset all angles\n"
                       "SET_ZERO/SZ - Set zero point\n"
                       "RESET_ZERO/RZ - Reset zero point\n"
                       "GET_DEVICE_INFO - Get device information\n"
                       "SET_DEVICE_NAME:NewName - Change device name\n"
                       "RESET_DEVICE_NAME - Reset device name to default\n"
                       "GET_DEVICE_NAME - Get current device name\n"
                       "PRINT_DEBUG - Print debug information\n"
                       "HELP/? - Show this help";
          webSocket.sendTXT(num, help);
        }
        else {
          String errorMsg = "ERROR: Unknown command. Send HELP for available commands.";
          webSocket.sendTXT(num, errorMsg);
        }
      }
      break;
      
    case WStype_ERROR:
      Serial.printf("[%u] WebSocket error\n", num);
      break;
      
    case WStype_BIN:
      // Обработка бинарных данных (если нужно)
      break;
  }
}

// Функция для подключения к многопользовательскому сокет-серверу
// В функции connectToSocketServer():
void connectToSocketServer() {
  Serial.println("Attempting to connect to multi-user socket server...");
  SOCKET_SERVER_HOST = getGatewayIP(); // Теперь это будет работать
  Serial.print("Host: "); Serial.print(SOCKET_SERVER_HOST);
  Serial.print(" Port: "); Serial.println(SOCKET_SERVER_PORT);
  
  // Настраиваем клиент WebSocket
  socketClient.begin(SOCKET_SERVER_HOST.c_str(), SOCKET_SERVER_PORT, "/");
  socketClient.onEvent(socketClientEvent);
  socketClient.setReconnectInterval(SOCKET_RECONNECT_INTERVAL);
  
  Serial.println("Socket client configured");
}

void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
}

void handleOptions() {
  addCORSHeaders();
  server.send(200, "text/plain", "");
}

void handleAPIStatus() {
  addCORSHeaders();
  
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  String json = "{";
  json += "\"status\":\"running\",";
  json += "\"device\":\"" + deviceName + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"pitch\":" + String(pitch, 2) + ",";
  json += "\"roll\":" + String(roll, 2) + ",";
  json += "\"yaw\":" + String(yaw, 2) + ",";
  json += "\"relPitch\":" + String(relPitch, 2) + ",";
  json += "\"relRoll\":" + String(relRoll, 2) + ",";
  json += "\"relYaw\":" + String(relYaw, 2) + ",";
  json += "\"accPitch\":" + String(accumulatedPitch, 2) + ",";
  json += "\"accRoll\":" + String(accumulatedRoll, 2) + ",";
  json += "\"accYaw\":" + String(accumulatedYaw, 2) + ",";
  json += "\"zeroSet\":" + String(zeroSet ? "true" : "false") + ",";
  json += "\"socketServerConnected\":" + String(socketServerConnected ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetZero() {
  addCORSHeaders();
  setZeroPoint();
  String response = "{\"status\":\"ok\",\"message\":\"Zero point set\",\"device\":\"" + deviceName + "\"}";
  server.send(200, "application/json", response);
}

void handleResetZero() {
  addCORSHeaders();
  resetZeroPoint();
  String response = "{\"status\":\"ok\",\"message\":\"Zero point reset\",\"device\":\"" + deviceName + "\"}";
  server.send(200, "application/json", response);
}

void handleResetAngles() {
  addCORSHeaders();
  resetAllAngles();
  String response = "{\"status\":\"ok\",\"message\":\"All angles reset\",\"device\":\"" + deviceName + "\"}";
  server.send(200, "application/json", response);
}

void handleDeviceInfo() {
  addCORSHeaders();
  String json = "{";
  json += "\"device\":\"" + deviceName + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  json += "\"firmware\":\"VR_Headset_v1.0\",";
  json += "\"sensor\":\"MPU6050\",";
  json += "\"unlimited_angles\":\"true\",";
  json += "\"eeprom\":\"enabled\",";
  json += "\"socket_server_connected\":" + String(socketServerConnected ? "true" : "false") + ",";
  json += "\"status\":\"running\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetDeviceName() {
  addCORSHeaders();
  
  if (server.method() == HTTP_POST) {
    String newName = server.arg("name");
    newName.trim();
    
    if (newName.length() == 0) {
      String response = "{\"status\":\"error\",\"message\":\"Device name cannot be empty\"}";
      server.send(400, "application/json", response);
      return;
    }
    
    if (newName.length() > MAX_DEVICE_NAME_LENGTH - 1) {
      String response = "{\"status\":\"error\",\"message\":\"Device name too long (max " + String(MAX_DEVICE_NAME_LENGTH - 1) + " characters)\"}";
      server.send(400, "application/json", response);
      return;
    }
    
    String oldName = deviceName;
    changeDeviceName(newName);
    
    String response = "{\"status\":\"ok\",\"message\":\"Device name changed\",\"old_name\":\"" + oldName + "\",\"new_name\":\"" + deviceName + "\"}";
    server.send(200, "application/json", response);
  } else {
    String response = "{\"status\":\"error\",\"message\":\"Only POST method allowed\"}";
    server.send(405, "application/json", response);
  }
}

void handleGetDeviceName() {
  addCORSHeaders();
  String json = "{\"device_name\":\"" + deviceName + "\"}";
  server.send(200, "application/json", json);
}

void handleResetDeviceName() {
  addCORSHeaders();
  String oldName = deviceName;
  resetDeviceName();
  String response = "{\"status\":\"ok\",\"message\":\"Device name reset to default\",\"old_name\":\"" + oldName + "\",\"new_name\":\"" + deviceName + "\"}";
  server.send(200, "application/json", response);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>MPU6050 Sensor Data - )rawliteral" + deviceName + R"rawliteral(</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; }
        .data { background: white; padding: 20px; margin: 10px 0; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        .value { font-size: 24px; font-weight: bold; color: #2c3e50; }
        .status { padding: 15px; margin: 10px 0; border-radius: 8px; font-weight: bold; }
        .connected { background: #d4edda; color: #155724; border: 2px solid #c3e6cb; }
        .disconnected { background: #f8d7da; color: #721c24; border: 2px solid #f5c6cb; }
        .controls { margin: 20px 0; }
        button { padding: 12px 20px; margin: 8px; border: none; border-radius: 6px; cursor: pointer; font-size: 16px; transition: all 0.3s; }
        button:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.2); }
        .btn-primary { background: #007bff; color: white; }
        .btn-warning { background: #ffc107; color: black; }
        .btn-danger { background: #dc3545; color: white; }
        .btn-success { background: #28a745; color: white; }
        .btn-info { background: #17a2b8; color: white; }
        .btn-secondary { background: #6c757d; color: white; }
        .zero-controls { background: #e8f5e8; padding: 20px; margin: 20px 0; border-radius: 10px; border-left: 5px solid #28a745; }
        .device-controls { background: #e3f2fd; padding: 20px; margin: 20px 0; border-radius: 10px; border-left: 5px solid #2196f3; }
        .data-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 15px; }
        .data-item { background: #f8f9fa; padding: 15px; border-radius: 8px; border-left: 4px solid #007bff; }
        .data-label { font-weight: bold; color: #495057; margin-bottom: 5px; }
        h1 { color: #2c3e50; text-align: center; margin-bottom: 30px; }
        h3 { color: #343a40; margin-bottom: 15px; }
        .device-info { background: #e3f2fd; padding: 15px; border-radius: 8px; margin: 10px 0; border-left: 4px solid #2196f3; }
        .unlimited-badge { background: #28a745; color: white; padding: 2px 8px; border-radius: 12px; font-size: 12px; margin-left: 10px; }
        .eeprom-badge { background: #ffc107; color: black; padding: 2px 8px; border-radius: 12px; font-size: 12px; margin-left: 5px; }
        .socket-badge { background: #6f42c1; color: white; padding: 2px 8px; border-radius: 12px; font-size: 12px; margin-left: 5px; }
        .info-text { font-size: 12px; color: #666; margin-top: 5px; }
        .name-input { padding: 8px 12px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; margin-right: 10px; width: 200px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>MPU6050 Sensor Data - )rawliteral" + deviceName + R"rawliteral( 
            <span class="unlimited-badge">UNLIMITED ANGLES</span>
            <span class="eeprom-badge">EEPROM</span>
            <span class="socket-badge">MULTI-USER</span>
        </h1>
        
        <div class="device-info">
            <strong>Device:</strong> <span id="deviceName">)rawliteral" + deviceName + R"rawliteral(</span> | 
            <strong>IP:</strong> <span id="deviceIP">)</span> | 
            <strong>Status:</strong> <span id="deviceStatus">Connected</span> |
            <strong>Socket Server:</strong> <span id="socketServerStatus" style="color: #dc3545;">Disconnected</span>
        </div>
        
        <div class="status" id="status">Disconnected</div>
        
        <div class="data">
            <h3>Sensor Readings <span style="font-size: 14px; color: #28a745;">(Unlimited Rotation)</span></h3>
            <div class="data-grid">
                <div class="data-item">
                    <div class="data-label">Pitch (Display)</div>
                    <div class="value" id="pitch">0</div>
                    <div class="info-text">Limited to ±180° for display</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Roll (Display)</div>
                    <div class="value" id="roll">0</div>
                    <div class="info-text">Limited to ±180° for display</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Yaw (Display)</div>
                    <div class="value" id="yaw">0</div>
                    <div class="info-text">Limited to ±180° for display</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Relative Pitch <span style="color: #28a745;">★</span></div>
                    <div class="value" id="relPitch">0</div>
                    <div class="info-text">Unlimited accumulation</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Relative Roll <span style="color: #28a745;">★</span></div>
                    <div class="value" id="relRoll">0</div>
                    <div class="info-text">Unlimited accumulation</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Relative Yaw <span style="color: #28a745;">★</span></div>
                    <div class="value" id="relYaw">0</div>
                    <div class="info-text">Unlimited accumulation</div>
                </div>
            </div>
        </div>
        
        <div class="zero-controls">
            <h3>Zero Point Control</h3>
            <button class="btn-success" onclick="sendCommand('SET_ZERO')">Set Zero Point</button>
            <button class="btn-warning" onclick="sendCommand('RESET_ZERO')">Reset Zero</button>
            <button class="btn-danger" onclick="sendCommand('RESET_ANGLES')">Reset All Angles</button>
            <div style="margin-top: 15px; padding: 10px; background: white; border-radius: 5px;">
                <div style="font-size: 14px; color: #666;">
                    <strong>Zero Point:</strong> <span id="zeroStatus" style="color: #dc3545; font-weight: bold;">Not Set</span>
                </div>
            </div>
        </div>
        
        <div class="device-controls">
            <h3>Device Management <span style="font-size: 14px; color: #2196f3;">(EEPROM Storage)</span></h3>
            <div style="margin-bottom: 15px;">
                <input type="text" id="newDeviceName" class="name-input" placeholder="New device name" maxlength="31">
                <button class="btn-primary" onclick="changeDeviceName()">Change Device Name</button>
            </div>
            <button class="btn-warning" onclick="sendCommand('GET_DEVICE_NAME')">Get Current Name</button>
            <button class="btn-secondary" onclick="sendCommand('RESET_DEVICE_NAME')">Reset to Default</button>
            <button class="btn-info" onclick="sendCommand('GET_DEVICE_INFO')">Device Info</button>
        </div>
        
        <div class="controls">
            <h3>Sensor Controls</h3>
            <button class="btn-primary" onclick="sendCommand('GET_DATA')">Get Sensor Data</button>
            <button class="btn-warning" onclick="sendCommand('RECALIBRATE')">Recalibrate</button>
            <button class="btn-info" onclick="sendCommand('PRINT_DEBUG')">Debug Info</button>
            <button class="btn-secondary" onclick="sendCommand('HELP')">Help</button>
        </div>
        
        <div class="data">
            <h3>Last Message</h3>
            <div id="lastMessage" style="background: #f8f9fa; padding: 10px; border-radius: 5px; font-family: monospace; min-height: 20px;">No data received</div>
        </div>
    </div>

    <script>
        let ws = null;
        let statusDiv = document.getElementById('status');
        let lastMessageDiv = document.getElementById('lastMessage');
        let zeroStatusSpan = document.getElementById('zeroStatus');
        let deviceIP = document.getElementById('deviceIP');
        let deviceStatus = document.getElementById('deviceStatus');
        let deviceNameSpan = document.getElementById('deviceName');
        let socketServerStatus = document.getElementById('socketServerStatus');
        
        function connectWebSocket() {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = protocol + '//' + window.location.hostname + ':81';
            
            ws = new WebSocket(wsUrl);
            
            ws.onopen = function() {
                console.log('WebSocket connected');
                statusDiv.textContent = 'Connected';
                statusDiv.className = 'status connected';
                deviceStatus.textContent = 'Connected';
                deviceStatus.style.color = '#28a745';
            };
            
            ws.onmessage = function(event) {
                console.log('Received:', event.data);
                lastMessageDiv.textContent = event.data;
                
                // Parse device connected message
                if (event.data.startsWith('DEVICE_CONNECTED:')) {
                    const device = event.data.split(':')[1];
                    console.log('Device connected:', device);
                }
                
                // Parse device info
                if (event.data.startsWith('DEVICE_INFO:')) {
                    const parts = event.data.split(',');
                    for (let part of parts) {
                        if (part.startsWith('IP:')) {
                            deviceIP.textContent = part.split(':')[1];
                        }
                        if (part.startsWith('SOCKET_SERVER:')) {
                            const status = part.split(':')[1];
                            socketServerStatus.textContent = status.charAt(0).toUpperCase() + status.slice(1);
                            socketServerStatus.style.color = status === 'connected' ? '#28a745' : '#dc3545';
                        }
                    }
                }
                
                // Parse device name changes
                if (event.data.startsWith('DEVICE_NAME_CHANGED:')) {
                    const newName = event.data.split(':')[1];
                    deviceNameSpan.textContent = newName;
                    document.title = 'MPU6050 Sensor Data - ' + newName;
                    showNotification('Device name changed to: ' + newName, 'success');
                }
                
                if (event.data.startsWith('CURRENT_DEVICE_NAME:')) {
                    const currentName = event.data.split(':')[1];
                    showNotification('Current device name: ' + currentName, 'info');
                }
                
                if (event.data.startsWith('DEVICE_NAME_RESET:')) {
                    const newName = event.data.split(':')[1];
                    deviceNameSpan.textContent = newName;
                    document.title = 'MPU6050 Sensor Data - ' + newName;
                    showNotification('Device name reset to: ' + newName, 'info');
                }
                
                // Parse sensor data
                if (event.data.includes('PITCH:') && event.data.includes('ROLL:') && event.data.includes('YAW:')) {
                    const data = event.data;
                    
                    // Parse all data fields
                    const deviceMatch = data.match(/DEVICE:([^,]+)/);
                    const pitchMatch = data.match(/PITCH:([-\d.]+)/);
                    const rollMatch = data.match(/ROLL:([-\d.]+)/);
                    const yawMatch = data.match(/YAW:([-\d.]+)/);
                    const relPitchMatch = data.match(/REL_PITCH:([-\d.]+)/);
                    const relRollMatch = data.match(/REL_ROLL:([-\d.]+)/);
                    const relYawMatch = data.match(/REL_YAW:([-\d.]+)/);
                    const zeroSetMatch = data.match(/ZERO_SET:(true|false)/);
                    
                    if (deviceMatch) {
                        deviceNameSpan.textContent = deviceMatch[1];
                        document.title = 'MPU6050 Sensor Data - ' + deviceMatch[1];
                    }
                    if (pitchMatch) {
                        const pitch = parseFloat(pitchMatch[1]);
                        document.getElementById('pitch').textContent = pitch.toFixed(1) + '°';
                    }
                    if (rollMatch) {
                        const roll = parseFloat(rollMatch[1]);
                        document.getElementById('roll').textContent = roll.toFixed(1) + '°';
                    }
                    if (yawMatch) {
                        const yaw = parseFloat(yawMatch[1]);
                        document.getElementById('yaw').textContent = yaw.toFixed(1) + '°';
                    }
                    if (relPitchMatch) {
                        const relPitch = parseFloat(relPitchMatch[1]);
                        document.getElementById('relPitch').textContent = relPitch.toFixed(1) + '°';
                    }
                    if (relRollMatch) {
                        const relRoll = parseFloat(relRollMatch[1]);
                        document.getElementById('relRoll').textContent = relRoll.toFixed(1) + '°';
                    }
                    if (relYawMatch) {
                        const relYaw = parseFloat(relYawMatch[1]);
                        document.getElementById('relYaw').textContent = relYaw.toFixed(1) + '°';
                    }
                    if (zeroSetMatch) {
                        const zeroSet = zeroSetMatch[1] === 'true';
                        zeroStatusSpan.textContent = zeroSet ? 'Set' : 'Not Set';
                        zeroStatusSpan.style.color = zeroSet ? '#28a745' : '#dc3545';
                    }
                }
                
                // Parse zero point messages
                if (event.data === 'ZERO_POINT_SET') {
                    zeroStatusSpan.textContent = 'Set';
                    zeroStatusSpan.style.color = '#28a745';
                    showNotification('Zero point set successfully', 'success');
                }
                if (event.data === 'ZERO_POINT_RESET') {
                    zeroStatusSpan.textContent = 'Not Set';
                    zeroStatusSpan.style.color = '#dc3545';
                    showNotification('Zero point reset', 'info');
                }
                if (event.data.startsWith('ZERO_SET:')) {
                    zeroStatusSpan.textContent = 'Set';
                    zeroStatusSpan.style.color = '#28a745';
                }
                if (event.data === 'ZERO_RESET') {
                    zeroStatusSpan.textContent = 'Not Set';
                    zeroStatusSpan.style.color = '#dc3545';
                }
            };
            
            ws.onclose = function() {
                console.log('WebSocket disconnected');
                statusDiv.textContent = 'Disconnected';
                statusDiv.className = 'status disconnected';
                deviceStatus.textContent = 'Disconnected';
                deviceStatus.style.color = '#dc3545';
                
                setTimeout(connectWebSocket, 2000);
            };
            
            ws.onerror = function(error) {
                console.error('WebSocket error:', error);
            };
        }
        
        function sendCommand(command) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(command);
                console.log('Sent command:', command);
            } else {
                console.log('WebSocket not connected');
                showNotification('WebSocket not connected', 'error');
            }
        }
        
        function changeDeviceName() {
            const newNameInput = document.getElementById('newDeviceName');
            const newName = newNameInput.value.trim();
            
            if (newName.length === 0) {
                showNotification('Please enter a device name', 'error');
                return;
            }
            
            if (newName.length > 31) {
                showNotification('Device name too long (max 31 characters)', 'error');
                return;
            }
            
            sendCommand('SET_DEVICE_NAME:' + newName);
            newNameInput.value = ''; // Clear input field
        }
        
        function showNotification(message, type = 'info') {
            // Create notification element
            const notification = document.createElement('div');
            notification.textContent = message;
            notification.style.cssText = `
                position: fixed;
                top: 20px;
                right: 20px;
                padding: 15px 20px;
                border-radius: 5px;
                color: white;
                font-weight: bold;
                z-index: 1000;
                background: ${type === 'success' ? '#28a745' : type === 'error' ? '#dc3545' : '#17a2b8'};
                box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            `;
            
            document.body.appendChild(notification);
            
            // Remove after 3 seconds
            setTimeout(() => {
                if (notification.parentNode) {
                    notification.parentNode.removeChild(notification);
                }
            }, 3000);
        }

        // Initialize when page loads
        window.addEventListener('load', function() {
            connectWebSocket();
            // Get device IP from current URL
            deviceIP.textContent = window.location.hostname;
            
            // Focus on name input field
            document.getElementById('newDeviceName').focus();
        });
    </script>
</body>
</html>
)rawliteral";

  addCORSHeaders();
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Даем время для инициализации Serial
  
  Serial.println("\n\nStarting MPU6050 Sensor Server...");
  
  // Инициализация EEPROM и загрузка имени устройства
  deviceName = loadDeviceNameFromEEPROM();
  
  // Устанавливаем имя хоста
  WiFi.hostname(deviceName.c_str());
  
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    Serial.println("Device Name: " + deviceName);
    Serial.println("UNLIMITED ANGLES: All relative angles can exceed 360°");
    Serial.println("EEPROM: Device name storage enabled");
    
    // Подключаемся к многопользовательскому сокет-серверу
    connectToSocketServer();
  } else {
    Serial.println("\nFailed to connect to WiFi!");
    return;
  }
  
  // Инициализация MPU6050
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    delay(1000);
    ESP.restart();
  }
  
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
  
  calibrateSensor();
  
  // Настройка серверов
  server.on("/", handleRoot);
  server.on("/api/status", HTTP_GET, handleAPIStatus);
  server.on("/api/device", HTTP_GET, handleDeviceInfo);
  server.on("/api/device/name", HTTP_GET, handleGetDeviceName);
  server.on("/api/device/name", HTTP_POST, handleSetDeviceName);
  server.on("/api/device/reset", HTTP_POST, handleResetDeviceName);
  server.on("/api/setZero", HTTP_POST, handleSetZero);
  server.on("/api/resetZero", HTTP_POST, handleResetZero);
  server.on("/api/resetAngles", HTTP_POST, handleResetAngles);
  
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.on("/api/device", HTTP_OPTIONS, handleOptions);
  server.on("/api/device/name", HTTP_OPTIONS, handleOptions);
  server.on("/api/device/reset", HTTP_OPTIONS, handleOptions);
  server.on("/api/setZero", HTTP_OPTIONS, handleOptions);
  server.on("/api/resetZero", HTTP_OPTIONS, handleOptions);
  server.on("/api/resetAngles", HTTP_OPTIONS, handleOptions);
  
  server.enableCORS(true);
  server.begin();
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("HTTP server started on port 80");
  Serial.println("WebSocket server started on port 81");
  Serial.println("Multi-user socket client configured for: " + String(SOCKET_SERVER_HOST) + ":" + String(SOCKET_SERVER_PORT));
  Serial.println("All relative angles (Pitch, Roll, Yaw) now support unlimited rotation beyond 360°");
  Serial.println("Device name management commands available:");
  Serial.println("  SET_DEVICE_NAME:NewName - Change device name");
  Serial.println("  RESET_DEVICE_NAME - Reset to default name");
  Serial.println("  GET_DEVICE_NAME - Get current name");
  Serial.println("  HELP - Show all available commands");
  
  lastDataSend = millis();
  lastTime = millis();
  lastSocketSend = millis();
}

void loop() {
  server.handleClient();
  webSocket.loop();
  socketClient.loop(); // Обработка сокет-клиента
  
  // Попытка переподключения к сокет-серверу, если соединение потеряно
  if (!socketServerConnected && millis() - lastSocketReconnectAttempt > SOCKET_RECONNECT_INTERVAL) {
    Serial.println("Attempting to reconnect to socket server...");
    connectToSocketServer();
    lastSocketReconnectAttempt = millis();
  }
  
  if (!calibrated) return;
  
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    Serial.println("Error reading MPU6050");
    delay(100);
    return;
  }
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0 || deltaTime > 1.0) {
    deltaTime = 0.01; // Защита от больших deltaTime
  }
  lastTime = currentTime;
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // ВСЕ оси обновляются ТОЛЬКО данными гироскопа для бесконечного суммирования
  pitch += gyroX * deltaTime * 180.0 / PI;
  roll += gyroY * deltaTime * 180.0 / PI;
  yaw += gyroZ * deltaTime * 180.0 / PI;
  
  // Ограничение углов для отображения в диапазоне -180 до 180 градусов
  if (pitch > 180) pitch -= 360;
  else if (pitch < -180) pitch += 360;
  
  if (roll > 180) roll -= 360;
  else if (roll < -180) roll += 360;
  
  if (yaw > 180) yaw -= 360;
  else if (yaw < -180) yaw += 360;
  
  // Отправка данных на локальный WebSocket
  if (clientConnected && (currentTime - lastDataSend >= SEND_INTERVAL)) {
    if (dataChanged() || lastDataSend == 0) {
      sendSensorData();
      lastDataSend = currentTime;
    }
  }
  
  // Отправка данных на многопользовательский сокет-сервер
  if (socketServerConnected && (currentTime - lastSocketSend >= SOCKET_SEND_INTERVAL)) {
    sendDataToSocketServer();
    lastSocketSend = currentTime;
  }
  
  delay(10);
}
