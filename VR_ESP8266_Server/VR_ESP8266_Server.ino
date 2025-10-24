#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Hash.h>

// Настройки точки доступа
const char* ap_ssid = "ESP8266_AP";
const char* ap_password = "12345678";

// Web сервер на порту 80
ESP8266WebServer server(80);

// WebSocket сервер на порту 81
WebSocketsServer webSocket = WebSocketsServer(81);

// Структура для хранения данных клиентов
struct ClientData {
  String deviceId;
  float pitch = 0;
  float roll = 0;
  float yaw = 0;
  float relPitch = 0;
  float relRoll = 0;
  float relYaw = 0;
  float accPitch = 0;
  float accRoll = 0;
  float accYaw = 0;
  bool zeroSet = false;
  float zeroPitch = 0;
  float zeroRoll = 0;
  float zeroYaw = 0;
  unsigned long lastUpdate = 0;
  bool connected = false;
  float temperature = 0;
  int batteryLevel = 100;
  int signalStrength = 0;
  bool isCalibrating = false;
  String deviceType = "MPU6050";
  String firmwareVersion = "1.0";
  bool dataStreamActive = false;
  String sendMode = "MOVEMENT_BASED";
  
  // Новые поля для визуализации
  float accX = 0;
  float accY = 0;
  float accZ = 0;
  float gyroX = 0;
  float gyroY = 0;
  float gyroZ = 0;
  unsigned long packetCount = 0;
  float movementMagnitude = 0;
};

// Максимальное количество клиентов
const int MAX_CLIENTS = 10;
ClientData clients[MAX_CLIENTS];

// Время жизни данных (5 секунд)
const unsigned long DATA_TIMEOUT = 5000;

// Статистика сервера
struct ServerStats {
  unsigned long startTime;
  unsigned long totalPackets = 0;
  unsigned long connectedClients = 0;
  unsigned long dataRate = 0; // пакетов в секунду
} serverStats;

// Поиск индекса клиента по deviceId
int findClientIndex(String deviceId) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].deviceId == deviceId) {
      return i;
    }
  }
  return -1;
}

// Поиск свободного слота для нового клиента
int findFreeClientSlot() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i].connected) {
      return i;
    }
  }
  return -1;
}

// Вспомогательная функция для обновления полей клиента
void updateClientField(int clientIndex, String key, String value) {
  if (key == "PITCH") clients[clientIndex].pitch = value.toFloat();
  else if (key == "ROLL") clients[clientIndex].roll = value.toFloat();
  else if (key == "YAW") clients[clientIndex].yaw = value.toFloat();
  else if (key == "REL_PITCH") clients[clientIndex].relPitch = value.toFloat();
  else if (key == "REL_ROLL") clients[clientIndex].relRoll = value.toFloat();
  else if (key == "REL_YAW") clients[clientIndex].relYaw = value.toFloat();
  else if (key == "ACC_PITCH") clients[clientIndex].accPitch = value.toFloat();
  else if (key == "ACC_ROLL") clients[clientIndex].accRoll = value.toFloat();
  else if (key == "ACC_YAW") clients[clientIndex].accYaw = value.toFloat();
  else if (key == "ZERO_SET") clients[clientIndex].zeroSet = (value == "true");
  else if (key == "ZERO_PITCH") clients[clientIndex].zeroPitch = value.toFloat();
  else if (key == "ZERO_ROLL") clients[clientIndex].zeroRoll = value.toFloat();
  else if (key == "ZERO_YAW") clients[clientIndex].zeroYaw = value.toFloat();
  else if (key == "TEMP") clients[clientIndex].temperature = value.toFloat();
  else if (key == "BATTERY") clients[clientIndex].batteryLevel = value.toInt();
  else if (key == "RSSI") clients[clientIndex].signalStrength = value.toInt();
  else if (key == "CALIBRATING") clients[clientIndex].isCalibrating = (value == "true");
  else if (key == "DEVICE_TYPE") clients[clientIndex].deviceType = value;
  else if (key == "FW_VERSION") clients[clientIndex].firmwareVersion = value;
  else if (key == "ACC_X") clients[clientIndex].accX = value.toFloat();
  else if (key == "ACC_Y") clients[clientIndex].accY = value.toFloat();
  else if (key == "ACC_Z") clients[clientIndex].accZ = value.toFloat();
  else if (key == "GYRO_X") clients[clientIndex].gyroX = value.toFloat();
  else if (key == "GYRO_Y") clients[clientIndex].gyroY = value.toFloat();
  else if (key == "GYRO_Z") clients[clientIndex].gyroZ = value.toFloat();
}

// Обновление данных клиента
void updateClientData(String deviceId, String data) {
  int clientIndex = findClientIndex(deviceId);
  
  if (clientIndex == -1) {
    clientIndex = findFreeClientSlot();
    if (clientIndex == -1) {
      Serial.println("No free slots for new client: " + deviceId);
      return;
    }
    clients[clientIndex].deviceId = deviceId;
    clients[clientIndex].connected = true;
    clients[clientIndex].packetCount = 0;
    Serial.println("New client registered: " + deviceId);
    serverStats.connectedClients++;
  }
  
  clients[clientIndex].lastUpdate = millis();
  clients[clientIndex].packetCount++;
  serverStats.totalPackets++;
  
  // Парсинг данных
  int start = 0;
  int end = data.indexOf(',');
  while (end != -1) {
    String pair = data.substring(start, end);
    int colon = pair.indexOf(':');
    if (colon != -1) {
      String key = pair.substring(0, colon);
      String value = pair.substring(colon + 1);
      updateClientField(clientIndex, key, value);
    }
    start = end + 1;
    end = data.indexOf(',', start);
  }
  
  // Обработка последней пары
  String pair = data.substring(start);
  int colon = pair.indexOf(':');
  if (colon != -1) {
    String key = pair.substring(0, colon);
    String value = pair.substring(colon + 1);
    updateClientField(clientIndex, key, value);
  }
  
  // Расчет величины движения для визуализации
  clients[clientIndex].movementMagnitude = sqrt(
    sq(clients[clientIndex].relPitch) + 
    sq(clients[clientIndex].relRoll) + 
    sq(clients[clientIndex].relYaw)
  );
}

// Очистка устаревших клиентов
void cleanupOldClients() {
  unsigned long currentTime = millis();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected && (currentTime - clients[i].lastUpdate > DATA_TIMEOUT)) {
      Serial.println("Client timeout: " + clients[i].deviceId);
      clients[i].connected = false;
      clients[i].deviceId = "";
      clients[i].dataStreamActive = false;
      serverStats.connectedClients--;
    }
  }
}

// Отправка данных всем WebSocket клиентам
void broadcastData() {
  static unsigned long lastStatsUpdate = 0;
  unsigned long currentTime = millis();
  
  // Обновление статистики скорости данных
  if (currentTime - lastStatsUpdate >= 1000) {
    serverStats.dataRate = serverStats.totalPackets;
    serverStats.totalPackets = 0;
    lastStatsUpdate = currentTime;
  }
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      String data = "CLIENT:" + clients[i].deviceId + 
                   ",PITCH:" + String(clients[i].pitch, 1) + 
                   ",ROLL:" + String(clients[i].roll, 1) + 
                   ",YAW:" + String(clients[i].yaw, 1) +
                   ",REL_PITCH:" + String(clients[i].relPitch, 2) +
                   ",REL_ROLL:" + String(clients[i].relRoll, 2) +
                   ",REL_YAW:" + String(clients[i].relYaw, 2) +
                   ",ACC_PITCH:" + String(clients[i].accPitch, 2) +
                   ",ACC_ROLL:" + String(clients[i].accRoll, 2) +
                   ",ACC_YAW:" + String(clients[i].accYaw, 2) +
                   ",ZERO_SET:" + String(clients[i].zeroSet ? "true" : "false") +
                   ",ZERO_PITCH:" + String(clients[i].zeroPitch, 2) +
                   ",ZERO_ROLL:" + String(clients[i].zeroRoll, 2) +
                   ",ZERO_YAW:" + String(clients[i].zeroYaw, 2) +
                   ",TEMP:" + String(clients[i].temperature, 1) +
                   ",BATTERY:" + String(clients[i].batteryLevel) +
                   ",RSSI:" + String(clients[i].signalStrength) +
                   ",CALIBRATING:" + String(clients[i].isCalibrating ? "true" : "false") +
                   ",DEVICE_TYPE:" + clients[i].deviceType +
                   ",FW_VERSION:" + clients[i].firmwareVersion +
                   ",STREAM_ACTIVE:" + String(clients[i].dataStreamActive ? "true" : "false") +
                   ",SEND_MODE:" + clients[i].sendMode +
                   ",MOVEMENT_MAG:" + String(clients[i].movementMagnitude, 2) +
                   ",PACKET_COUNT:" + String(clients[i].packetCount) +
                   ",ACC_X:" + String(clients[i].accX, 2) +
                   ",ACC_Y:" + String(clients[i].accY, 2) +
                   ",ACC_Z:" + String(clients[i].accZ, 2) +
                   ",GYRO_X:" + String(clients[i].gyroX, 2) +
                   ",GYRO_Y:" + String(clients[i].gyroY, 2) +
                   ",GYRO_Z:" + String(clients[i].gyroZ, 2);
      webSocket.broadcastTXT(data);
    }
  }
  
  // Отправка статистики сервера
  String stats = "SERVER_STATS:Uptime:" + String(millis() - serverStats.startTime) +
                ",ConnectedClients:" + String(serverStats.connectedClients) +
                ",DataRate:" + String(serverStats.dataRate) + "pps" +
                ",FreeHeap:" + String(ESP.getFreeHeap()) +
                ",WiFiClients:" + String(WiFi.softAPgetStationNum());
  webSocket.broadcastTXT(stats);
}

// Проверка是否是 статусное сообщение
bool isStatusMessage(String message) {
  return message.startsWith("CLIENT_ID:") || 
         message.startsWith("RECALIBRATION_COMPLETE:") ||
         message.startsWith("ANGLES_RESET:") ||
         message.startsWith("ZERO_POINT_SET:") ||
         message.startsWith("ZERO_POINT_RESET:") ||
         message.startsWith("FORCE_CALIBRATION_COMPLETE:") ||
         message.startsWith("CURRENT_POSITION_SET_AS_ZERO:") ||
         message.startsWith("ZERO_SET_AT_CURRENT:") ||
         message.startsWith("CALIBRATION_STARTED:") ||
         message.startsWith("DATA_SENDING_STARTED:") ||
         message.startsWith("DATA_SENDING_STOPPED:") ||
         message.startsWith("DEVICE_READY:") ||
         message.startsWith("LOW_BATTERY:") ||
         message.startsWith("SENSOR_ERROR:") ||
         message.startsWith("STREAM_STARTED:") ||
         message.startsWith("STREAM_STOPPED:") ||
         message.startsWith("CALIBRATION_PROGRESS:") ||
         message.startsWith("ALL_ANGLES_RESET:");
}

// Обработка информации об устройстве
void processDeviceInfo(String message) {
  int colonPos = message.indexOf(':');
  if (colonPos != -1) {
    String deviceInfo = message.substring(colonPos + 1);
    int idPos = deviceInfo.indexOf("ID:");
    if (idPos != -1) {
      int commaPos = deviceInfo.indexOf(',', idPos);
      if (commaPos != -1) {
        String deviceId = deviceInfo.substring(idPos + 3, commaPos);
        int clientIndex = findClientIndex(deviceId);
        if (clientIndex != -1) {
          // Обновление информации об устройстве
          int typePos = deviceInfo.indexOf("TYPE:");
          if (typePos != -1) {
            int typeComma = deviceInfo.indexOf(',', typePos);
            if (typeComma != -1) {
              clients[clientIndex].deviceType = deviceInfo.substring(typePos + 5, typeComma);
            }
          }
          
          int fwPos = deviceInfo.indexOf("FW_VERSION:");
          if (fwPos != -1) {
            int fwComma = deviceInfo.indexOf(',', fwPos);
            if (fwComma != -1) {
              clients[clientIndex].firmwareVersion = deviceInfo.substring(fwPos + 11, fwComma);
            }
          }
          
          int modePos = deviceInfo.indexOf("SEND_MODE:");
          if (modePos != -1) {
            int modeComma = deviceInfo.indexOf(',', modePos);
            if (modeComma != -1) {
              clients[clientIndex].sendMode = deviceInfo.substring(modePos + 10, modeComma);
            }
          }
        }
      }
    }
  }
}

// Обработка команд для конкретного устройства
void handleDeviceCommand(String targetDevice, String cmd, uint8_t clientNum) {
  Serial.println("Command for " + targetDevice + ": " + cmd);
  
  int clientIndex = findClientIndex(targetDevice);
  if (clientIndex != -1 && clients[clientIndex].connected) {
    String cmdMsg = cmd;
    webSocket.sendTXT(clientNum, cmdMsg);
    Serial.println("Command sent to device: " + cmd);
    
    if (cmd == "START_STREAM") {
      clients[clientIndex].dataStreamActive = true;
      clients[clientIndex].sendMode = "CONTINUOUS";
    } else if (cmd == "STOP_STREAM") {
      clients[clientIndex].dataStreamActive = false;
      clients[clientIndex].sendMode = "MOVEMENT_BASED";
    }
    
    String confirmMsg = "COMMAND_SENT:" + targetDevice + ":" + cmd;
    webSocket.sendTXT(clientNum, confirmMsg);
  } else {
    String errorMsg = "DEVICE_NOT_FOUND:" + targetDevice;
    webSocket.sendTXT(clientNum, errorMsg);
  }
}

// Обработка широковещательных команд
void handleBroadcastCommand(String broadcastCmd, uint8_t clientNum) {
  Serial.println("Broadcast command: " + broadcastCmd);
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      String cmdMsg = broadcastCmd;
      webSocket.sendTXT(clientNum, cmdMsg);
      
      if (broadcastCmd == "START_STREAM") {
        clients[i].dataStreamActive = true;
        clients[i].sendMode = "CONTINUOUS";
      } else if (broadcastCmd == "STOP_STREAM") {
        clients[i].dataStreamActive = false;
        clients[i].sendMode = "MOVEMENT_BASED";
      }
    }
  }
  
  String confirmMsg = "BROADCAST_SENT:" + broadcastCmd;
  webSocket.sendTXT(clientNum, confirmMsg);
}

// Отправка списка устройств
void sendDeviceList(uint8_t clientNum) {
  String deviceList = "DEVICE_LIST:";
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      if (deviceList != "DEVICE_LIST:") {
        deviceList += ",";
      }
      deviceList += clients[i].deviceId + ":" + 
                   (clients[i].dataStreamActive ? "STREAMING" : "MOVEMENT") + ":" +
                   String(clients[i].movementMagnitude, 1);
    }
  }
  webSocket.sendTXT(clientNum, deviceList);
}

// Очистка неактивных устройств
void clearInactiveDevices(uint8_t clientNum) {
  int clearedCount = 0;
  unsigned long currentTime = millis();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected && (currentTime - clients[i].lastUpdate > DATA_TIMEOUT)) {
      Serial.println("Clearing inactive device: " + clients[i].deviceId);
      clients[i].connected = false;
      clients[i].deviceId = "";
      clients[i].dataStreamActive = false;
      clearedCount++;
      serverStats.connectedClients--;
    }
  }
  String clearMsg = "CLEARED_DEVICES:" + String(clearedCount);
  webSocket.sendTXT(clientNum, clearMsg);
}

// Отправка статуса сервера
void sendServerStatus(uint8_t clientNum) {
  int connectedCount = 0;
  int streamingCount = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      connectedCount++;
      if (clients[i].dataStreamActive) streamingCount++;
    }
  }
  String statusMsg = "SERVER_STATUS:Uptime:" + String(millis()) + 
                    ",ConnectedDevices:" + String(connectedCount) +
                    ",StreamingDevices:" + String(streamingCount) +
                    ",FreeMemory:" + String(ESP.getFreeHeap()) +
                    ",DataRate:" + String(serverStats.dataRate) + "pps" +
                    ",SendMode:EnhancedVisualization";
  webSocket.sendTXT(clientNum, statusMsg);
}

// Запуск потока данных
void startDataStream(uint8_t clientNum) {
  webSocket.broadcastTXT("START_STREAM");
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      clients[i].dataStreamActive = true;
      clients[i].sendMode = "CONTINUOUS";
    }
  }
  String response = "DATA_STREAM_STARTED:All devices set to continuous streaming";
  webSocket.sendTXT(clientNum, response);
}

// Остановка потока данных
void stopDataStream(uint8_t clientNum) {
  webSocket.broadcastTXT("STOP_STREAM");
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      clients[i].dataStreamActive = false;
      clients[i].sendMode = "MOVEMENT_BASED";
    }
  }
  String response = "DATA_STREAM_STOPPED:All devices set to movement-based sending";
  webSocket.sendTXT(clientNum, response);
}

// Отправка данных для визуализации
void sendVisualizationData(uint8_t clientNum) {
  String jsonData = "[";
  bool firstDevice = true;
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      if (!firstDevice) {
        jsonData += ",";
      }
      jsonData += "{";
      jsonData += "\"id\":\"" + clients[i].deviceId + "\",";
      jsonData += "\"pitch\":" + String(clients[i].pitch, 2) + ",";
      jsonData += "\"roll\":" + String(clients[i].roll, 2) + ",";
      jsonData += "\"yaw\":" + String(clients[i].yaw, 2) + ",";
      jsonData += "\"relPitch\":" + String(clients[i].relPitch, 2) + ",";
      jsonData += "\"relRoll\":" + String(clients[i].relRoll, 2) + ",";
      jsonData += "\"relYaw\":" + String(clients[i].relYaw, 2) + ",";
      jsonData += "\"movementMagnitude\":" + String(clients[i].movementMagnitude, 2) + ",";
      jsonData += "\"battery\":" + String(clients[i].batteryLevel) + ",";
      jsonData += "\"temperature\":" + String(clients[i].temperature, 1) + ",";
      jsonData += "\"streaming\":" + String(clients[i].dataStreamActive ? "true" : "false");
      jsonData += "}";
      firstDevice = false;
    }
  }
  jsonData += "]";
  
  String response = "VISUALIZATION_DATA:" + jsonData;
  webSocket.sendTXT(clientNum, response);
}

// Обработка команд
void handleCommand(String command, uint8_t clientNum) {
  if (command == "GET_ALL_DATA") {
    broadcastData();
  }
  else if (command.startsWith("SEND_TO_DEVICE:")) {
    String deviceCmd = command.substring(15);
    int colonPos = deviceCmd.indexOf(':');
    if (colonPos != -1) {
      String targetDevice = deviceCmd.substring(0, colonPos);
      String cmd = deviceCmd.substring(colonPos + 1);
      handleDeviceCommand(targetDevice, cmd, clientNum);
    }
  }
  else if (command.startsWith("BROADCAST:")) {
    String broadcastCmd = command.substring(10);
    handleBroadcastCommand(broadcastCmd, clientNum);
  }
  else if (command == "GET_DEVICE_LIST") {
    sendDeviceList(clientNum);
  }
  else if (command == "CLEAR_INACTIVE_DEVICES") {
    clearInactiveDevices(clientNum);
  }
  else if (command == "GET_SERVER_STATUS") {
    sendServerStatus(clientNum);
  }
  else if (command.startsWith("SET_UPDATE_RATE:")) {
    String rate = command.substring(16);
    String rateMsg = "UPDATE_RATE_SET:" + rate + "ms";
    webSocket.sendTXT(clientNum, rateMsg);
  }
  else if (command == "START_DATA_STREAM") {
    startDataStream(clientNum);
  }
  else if (command == "STOP_DATA_STREAM") {
    stopDataStream(clientNum);
  }
  else if (command == "FORCE_CALIBRATION") {
    webSocket.broadcastTXT("FORCE_CALIBRATE");
    String response = "FORCE_CALIBRATION_SENT:All devices will recalibrate";
    webSocket.sendTXT(clientNum, response);
  }
  else if (command == "GET_DEVICE_INFO_ALL") {
    webSocket.broadcastTXT("GET_INFO");
    String response = "DEVICE_INFO_REQUEST_SENT:Requesting info from all devices";
    webSocket.sendTXT(clientNum, response);
  }
  else if (command == "RESET_ALL_ANGLES") {
    webSocket.broadcastTXT("RESET_ALL_ANGLES");
    String response = "RESET_ALL_ANGLES_SENT:Resetting angles on all devices";
    webSocket.sendTXT(clientNum, response);
  }
  else if (command == "GET_VISUALIZATION_DATA") {
    sendVisualizationData(clientNum);
  }
}

// Обработчик WebSocket событий
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        String welcomeMsg = "SERVER:Welcome to VR Data Server - Enhanced Visualization";
        webSocket.sendTXT(num, welcomeMsg);
        
        // Отправка текущего состояния всех клиентов
        for (int i = 0; i < MAX_CLIENTS; i++) {
          if (clients[i].connected) {
            String data = "CLIENT:" + clients[i].deviceId + 
                         ",PITCH:" + String(clients[i].pitch, 1) + 
                         ",ROLL:" + String(clients[i].roll, 1) + 
                         ",YAW:" + String(clients[i].yaw, 1) +
                         ",REL_PITCH:" + String(clients[i].relPitch, 2) +
                         ",REL_ROLL:" + String(clients[i].relRoll, 2) +
                         ",REL_YAW:" + String(clients[i].relYaw, 2) +
                         ",MOVEMENT_MAG:" + String(clients[i].movementMagnitude, 2);
            webSocket.sendTXT(num, data);
          }
        }
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[%u] Received: %s\n", num, message);
        
        if (message.startsWith("deviceId:")) {
          int commaPos = message.indexOf(',');
          if (commaPos != -1) {
            String deviceId = message.substring(9, commaPos);
            String data = message.substring(commaPos + 1);
            updateClientData(deviceId, data);
            
            String ackMsg = "DATA_RECEIVED:" + deviceId;
            webSocket.sendTXT(num, ackMsg);
          }
        }
        else if (message.startsWith("CMD:")) {
          handleCommand(message.substring(4), num);
        }
        else if (isStatusMessage(message)) {
          webSocket.broadcastTXT(message);
          Serial.println("Status message broadcasted: " + message);
        }
        else if (message.startsWith("DEVICE_INFO:")) {
          webSocket.broadcastTXT(message);
          Serial.println("Device info broadcasted: " + message);
          processDeviceInfo(message);
        }
        else if (message.startsWith("DATA_STREAM:")) {
          webSocket.broadcastTXT(message);
        }
      }
      break;
  }
}

// Добавление CORS заголовков
void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

// Обработчик OPTIONS запросов
void handleOptions() {
  addCORSHeaders();
  server.send(200, "text/plain", "");
}

// HTML фрагменты
String getHTMLHeader() {
  return R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>VR Data Server - Enhanced 3D Visualization</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/three@0.128.0/examples/js/controls/OrbitControls.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: #333; overflow-x: hidden; }
        .container { display: grid; grid-template-columns: 300px 1fr 300px; grid-template-rows: auto 1fr auto; gap: 20px; padding: 20px; min-height: 100vh; }
        .header { grid-column: 1 / -1; background: rgba(255, 255, 255, 0.95); padding: 20px; border-radius: 15px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1); backdrop-filter: blur(10px); }
        .sidebar { background: rgba(255, 255, 255, 0.95); border-radius: 15px; padding: 20px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1); backdrop-filter: blur(10px); }
        .visualization { background: rgba(255, 255, 255, 0.95); border-radius: 15px; padding: 20px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1); backdrop-filter: blur(10px); position: relative; }
        .controls { background: rgba(255, 255, 255, 0.95); border-radius: 15px; padding: 20px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1); backdrop-filter: blur(10px); }
        h1, h2, h3 { color: #2c3e50; margin-bottom: 15px; }
        .status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; }
        .status-connected { background: #2ecc71; }
        .status-disconnected { background: #e74c3c; }
        .status-streaming { background: #3498db; }
        .device-card { background: #f8f9fa; border-radius: 10px; padding: 15px; margin-bottom: 10px; border-left: 4px solid #3498db; transition: all 0.3s ease; }
        .device-card:hover { transform: translateY(-2px); box-shadow: 0 4px 15px rgba(0, 0, 0, 0.1); }
        .device-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .device-id { font-weight: bold; color: #2c3e50; }
        .device-data { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; font-size: 12px; }
        .data-item { display: flex; justify-content: space-between; }
        .data-label { color: #7f8c8d; }
        .data-value { font-weight: bold; color: #2c3e50; }
        .movement-bar { height: 4px; background: #ecf0f1; border-radius: 2px; margin-top: 5px; overflow: hidden; }
        .movement-fill { height: 100%; background: linear-gradient(90deg, #2ecc71, #f1c40f, #e74c3c); transition: width 0.3s ease; }
        button { background: linear-gradient(135deg, #3498db, #2980b9); color: white; border: none; padding: 10px 15px; border-radius: 8px; cursor: pointer; font-size: 14px; margin: 5px; transition: all 0.3s ease; }
        button:hover { transform: translateY(-2px); box-shadow: 0 4px 15px rgba(52, 152, 219, 0.3); }
        button:active { transform: translateY(0); }
        .btn-success { background: linear-gradient(135deg, #2ecc71, #27ae60); }
        .btn-warning { background: linear-gradient(135deg, #f39c12, #e67e22); }
        .btn-danger { background: linear-gradient(135deg, #e74c3c, #c0392b); }
        .btn-info { background: linear-gradient(135deg, #9b59b6, #8e44ad); }
        .control-group { margin-bottom: 15px; }
        .control-group h3 { border-bottom: 2px solid #ecf0f1; padding-bottom: 5px; margin-bottom: 10px; }
        #visualizationCanvas { width: 100%; height: 500px; border-radius: 10px; background: #1a1a1a; }
        .chart-container { background: white; border-radius: 10px; padding: 15px; margin-top: 15px; }
        .server-stats { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 15px; }
        .stat-item { background: #ecf0f1; padding: 10px; border-radius: 8px; text-align: center; }
        .stat-value { font-size: 24px; font-weight: bold; color: #2c3e50; }
        .stat-label { font-size: 12px; color: #7f8c8d; }
        .notification { position: fixed; top: 20px; right: 20px; padding: 15px 20px; border-radius: 8px; color: white; box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2); z-index: 1000; transform: translateX(400px); transition: transform 0.3s ease; }
        .notification.show { transform: translateX(0); }
        .notification.success { background: #2ecc71; }
        .notification.error { background: #e74c3c; }
        .notification.info { background: #3498db; }
        .notification.warning { background: #f39c12; }
        .connection-status { padding: 10px; border-radius: 8px; margin-bottom: 15px; text-align: center; font-weight: bold; }
        .connected { background: #d5f4e6; color: #27ae60; border: 2px solid #2ecc71; }
        .disconnected { background: #fadbd8; color: #c0392b; border: 2px solid #e74c3c; }
    </style>
</head>
<body>
    )=====";
}

String getHeaderSection() {
  return R"=====(
    <div class="container">
        <!-- Header -->
        <div class="header">
            <h1>🚀 VR Data Server - Enhanced 3D Visualization</h1>
            <p>Real-time MPU6050 sensor data monitoring with 3D visualization</p>
            <div class="connection-status" id="connectionStatus">
                <span class="status-indicator status-disconnected"></span>
                WebSocket: Connecting...
            </div>
        </div>
    )=====";
}

String getDevicesSidebar() {
  return R"=====(
        <!-- Left Sidebar - Devices List -->
        <div class="sidebar">
            <h2>📱 Connected Devices</h2>
            <div id="devicesList">
                <div class="device-card">
                    <div class="device-header">
                        <span class="device-id">Waiting for data...</span>
                    </div>
                </div>
            </div>
            
            <div class="server-stats">
                <div class="stat-item">
                    <div class="stat-value" id="connectedCount">0</div>
                    <div class="stat-label">Connected</div>
                </div>
                <div class="stat-item">
                    <div class="stat-value" id="dataRate">0</div>
                    <div class="stat-label">Packets/s</div>
                </div>
            </div>
        </div>
    )=====";
}

String getVisualizationSection() {
  return R"=====(
        <!-- Main Visualization Area -->
        <div class="visualization">
            <h2>🎯 3D Visualization</h2>
            <div id="visualizationCanvas"></div>
            <div class="chart-container">
                <canvas id="movementChart"></canvas>
            </div>
        </div>
    )=====";
}

String getControlsSidebar() {
  return R"=====(
        <!-- Right Sidebar - Controls -->
        <div class="controls">
            <h2>🛠️ Controls</h2>
            
            <div class="control-group">
                <h3>Device Management</h3>
                <button onclick="sendCommand('GET_DEVICE_LIST')">Refresh Devices</button>
                <button onclick="sendCommand('CLEAR_INACTIVE_DEVICES')" class="btn-warning">Clear Inactive</button>
            </div>

            <div class="control-group">
                <h3>Data Stream</h3>
                <button onclick="sendCommand('START_DATA_STREAM')" class="btn-success">Start All Streams</button>
                <button onclick="sendCommand('STOP_DATA_STREAM')" class="btn-danger">Stop All Streams</button>
            </div>

            <div class="control-group">
                <h3>Calibration</h3>
                <button onclick="sendCommand('FORCE_CALIBRATION')" class="btn-warning">Force Calibration</button>
                <button onclick="sendCommand('RESET_ALL_ANGLES')" class="btn-info">Reset All Angles</button>
            </div>

            <div class="control-group">
                <h3>Visualization</h3>
                <button onclick="toggleViewMode()">Toggle View</button>
                <button onclick="resetCamera()">Reset Camera</button>
                <button onclick="downloadData()">Export Data</button>
            </div>

            <div class="control-group">
                <h3>Server Info</h3>
                <button onclick="sendCommand('GET_SERVER_STATUS')">Get Status</button>
                <button onclick="sendCommand('GET_VISUALIZATION_DATA')">Get Viz Data</button>
            </div>
        </div>
    </div>
    )=====";
}

String getNotificationSection() {
  return R"=====(
    <div id="notification" class="notification"></div>
    )=====";
}

String getJavaScriptCode() {
  return R"=====(
    <script>
        // WebSocket connection
        let ws = null;
        let devices = {};
        let scene, camera, renderer, controls;
        let deviceMeshes = {};
        let movementChart = null;
        let chartData = {
            labels: [],
            datasets: []
        };

        // Initialize WebSocket
        function connectWebSocket() {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = protocol + '//' + window.location.hostname + ':81';
            
            ws = new WebSocket(wsUrl);
            
            ws.onopen = function() {
                console.log('WebSocket connected');
                updateConnectionStatus(true);
                showNotification('Connected to server', 'success');
            };
            
            ws.onmessage = function(event) {
                handleWebSocketMessage(event.data);
            };
            
            ws.onclose = function() {
                console.log('WebSocket disconnected');
                updateConnectionStatus(false);
                setTimeout(connectWebSocket, 2000);
            };
            
            ws.onerror = function(error) {
                console.error('WebSocket error:', error);
                showNotification('WebSocket error', 'error');
            };
        }

        // Handle incoming WebSocket messages
        function handleWebSocketMessage(data) {
            console.log('Received:', data);
            
            if (data.startsWith('CLIENT:')) {
                updateDeviceData(data);
            } else if (data.startsWith('SERVER_STATS:')) {
                updateServerStats(data);
            } else if (data.startsWith('DEVICE_LIST:')) {
                updateDeviceList(data);
            } else if (data.startsWith('SERVER_STATUS:')) {
                showNotification('Server status updated', 'info');
            } else if (data.startsWith('DATA_RECEIVED:')) {
                showNotification('Data from ' + data.substring(14), 'success');
            }
        }

        // Update device data
        function updateDeviceData(data) {
            const parts = data.split(',');
            const deviceId = parts[0].substring(7);
            
            if (!devices[deviceId]) {
                devices[deviceId] = {};
                createDeviceCard(deviceId);
                create3DDevice(deviceId);
            }
            
            // Parse all data fields
            parts.forEach(part => {
                const [key, value] = part.split(':');
                if (key && value !== undefined) {
                    devices[deviceId][key] = value;
                }
            });
            
            updateDeviceDisplay(deviceId);
            update3DDevice(deviceId);
            updateMovementChart(deviceId);
        }

        // Create device card in sidebar
        function createDeviceCard(deviceId) {
            const devicesList = document.getElementById('devicesList');
            const card = document.createElement('div');
            card.className = 'device-card';
            card.id = 'card-' + deviceId;
            card.innerHTML = `
                <div class="device-header">
                    <span class="device-id">${deviceId}</span>
                    <span class="status-indicator status-connected" id="status-${deviceId}"></span>
                </div>
                <div class="device-data">
                    <div class="data-item">
                        <span class="data-label">Pitch:</span>
                        <span class="data-value" id="pitch-${deviceId}">0°</span>
                    </div>
                    <div class="data-item">
                        <span class="data-label">Roll:</span>
                        <span class="data-value" id="roll-${deviceId}">0°</span>
                    </div>
                    <div class="data-item">
                        <span class="data-label">Yaw:</span>
                        <span class="data-value" id="yaw-${deviceId}">0°</span>
                    </div>
                    <div class="data-item">
                        <span class="data-label">Movement:</span>
                        <span class="data-value" id="movement-${deviceId}">0</span>
                    </div>
                </div>
                <div class="movement-bar">
                    <div class="movement-fill" id="movementBar-${deviceId}" style="width: 0%"></div>
                </div>
            `;
            devicesList.appendChild(card);
        }

        // Update device display
        function updateDeviceDisplay(deviceId) {
            const device = devices[deviceId];
            if (!device) return;
            
            // Update basic data
            document.getElementById('pitch-' + deviceId).textContent = (device.PITCH || 0) + '°';
            document.getElementById('roll-' + deviceId).textContent = (device.ROLL || 0) + '°';
            document.getElementById('yaw-' + deviceId).textContent = (device.YAW || 0) + '°';
            
            // Update movement indicator
            const movement = parseFloat(device.MOVEMENT_MAG) || 0;
            document.getElementById('movement-' + deviceId).textContent = movement.toFixed(1);
            const movementPercent = Math.min(movement * 10, 100);
            document.getElementById('movementBar-' + deviceId).style.width = movementPercent + '%';
            
            // Update status
            const statusElement = document.getElementById('status-' + deviceId);
            if (device.STREAM_ACTIVE === 'true') {
                statusElement.className = 'status-indicator status-streaming';
            } else {
                statusElement.className = 'status-indicator status-connected';
            }
        }

        // Initialize 3D visualization
        function init3DVisualization() {
            const canvas = document.getElementById('visualizationCanvas');
            
            // Scene
            scene = new THREE.Scene();
            scene.background = new THREE.Color(0x1a1a1a);
            
            // Camera
            camera = new THREE.PerspectiveCamera(75, canvas.offsetWidth / canvas.offsetHeight, 0.1, 1000);
            camera.position.set(5, 5, 5);
            
            // Renderer
            renderer = new THREE.WebGLRenderer({ antialias: true });
            renderer.setSize(canvas.offsetWidth, canvas.offsetHeight);
            canvas.appendChild(renderer.domElement);
            
            // Controls
            controls = new THREE.OrbitControls(camera, renderer.domElement);
            controls.enableDamping = true;
            controls.dampingFactor = 0.05;
            
            // Lighting
            const ambientLight = new THREE.AmbientLight(0x404040, 0.6);
            scene.add(ambientLight);
            
            const directionalLight = new THREE.DirectionalLight(0xffffff, 0.8);
            directionalLight.position.set(10, 10, 5);
            scene.add(directionalLight);
            
            // Grid helper
            const gridHelper = new THREE.GridHelper(10, 10);
            scene.add(gridHelper);
            
            // Axes helper
            const axesHelper = new THREE.AxesHelper(3);
            scene.add(axesHelper);
            
            // Start animation loop
            animate();
        }

        // Create 3D representation of a device
        function create3DDevice(deviceId) {
            const color = new THREE.Color(Math.random(), Math.random(), Math.random());
            
            // Main device body
            const geometry = new THREE.BoxGeometry(1, 0.2, 0.5);
            const material = new THREE.MeshPhongMaterial({ color: color });
            const mesh = new THREE.Mesh(geometry, material);
            
            // Orientation indicator
            const arrowGeometry = new THREE.ConeGeometry(0.1, 0.3, 8);
            const arrowMaterial = new THREE.MeshPhongMaterial({ color: 0xff0000 });
            const arrow = new THREE.Mesh(arrowGeometry, arrowMaterial);
            arrow.position.y = 0.2;
            arrow.rotation.x = Math.PI;
            mesh.add(arrow);
            
            scene.add(mesh);
            deviceMeshes[deviceId] = mesh;
        }

        // Update 3D device position and rotation
        function update3DDevice(deviceId) {
            const mesh = deviceMeshes[deviceId];
            if (!mesh) return;
            
            const device = devices[deviceId];
            if (!device) return;
            
            // Convert degrees to radians for Three.js
            const pitch = (parseFloat(device.PITCH) || 0) * Math.PI / 180;
            const roll = (parseFloat(device.ROLL) || 0) * Math.PI / 180;
            const yaw = (parseFloat(device.YAW) || 0) * Math.PI / 180;
            
            // Apply rotation (order: Yaw -> Pitch -> Roll)
            mesh.rotation.set(pitch, yaw, roll);
            
            // Position devices in a circle
            const deviceCount = Object.keys(devices).length;
            const index = Object.keys(devices).indexOf(deviceId);
            const angle = (index / deviceCount) * Math.PI * 2;
            const radius = 3;
            
            mesh.position.x = Math.cos(angle) * radius;
            mesh.position.z = Math.sin(angle) * radius;
        }

        // Initialize movement chart
        function initMovementChart() {
            const ctx = document.getElementById('movementChart').getContext('2d');
            movementChart = new Chart(ctx, {
                type: 'line',
                data: chartData,
                options: {
                    responsive: true,
                    scales: {
                        y: {
                            beginAtZero: true,
                            title: {
                                display: true,
                                text: 'Movement Magnitude'
                            }
                        },
                        x: {
                            title: {
                                display: true,
                                text: 'Time'
                            }
                        }
                    }
                }
            });
        }

        // Update movement chart
        function updateMovementChart(deviceId) {
            const device = devices[deviceId];
            if (!device) return;
            
            const movement = parseFloat(device.MOVEMENT_MAG) || 0;
            const timestamp = new Date().toLocaleTimeString();
            
            // Add new data point
            chartData.labels.push(timestamp);
            if (chartData.labels.length > 20) {
                chartData.labels.shift();
            }
            
            // Find or create dataset for this device
            let dataset = chartData.datasets.find(ds => ds.label === deviceId);
            if (!dataset) {
                const color = 'hsl(' + (Object.keys(devices).length * 137.5) + ', 70%, 50%)';
                dataset = {
                    label: deviceId,
                    data: [],
                    borderColor: color,
                    backgroundColor: color + '20',
                    tension: 0.4,
                    fill: false
                };
                chartData.datasets.push(dataset);
            }
            
            dataset.data.push(movement);
            if (dataset.data.length > 20) {
                dataset.data.shift();
            }
            
            movementChart.update();
        }

        // Animation loop
        function animate() {
            requestAnimationFrame(animate);
            controls.update();
            renderer.render(scene, camera);
        }

        // Utility functions
        function sendCommand(command) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send('CMD:' + command);
                console.log('Sent command:', command);
            } else {
                showNotification('WebSocket not connected', 'error');
            }
        }

        function updateConnectionStatus(connected) {
            const statusElement = document.getElementById('connectionStatus');
            if (connected) {
                statusElement.innerHTML = '<span class="status-indicator status-connected"></span> WebSocket: Connected';
                statusElement.className = 'connection-status connected';
            } else {
                statusElement.innerHTML = '<span class="status-indicator status-disconnected"></span> WebSocket: Disconnected';
                statusElement.className = 'connection-status disconnected';
            }
        }

        function updateServerStats(data) {
            const stats = data.substring(13).split(',');
            stats.forEach(stat => {
                const [key, value] = stat.split(':');
                if (key === 'ConnectedClients') {
                    document.getElementById('connectedCount').textContent = value;
                } else if (key === 'DataRate') {
                    document.getElementById('dataRate').textContent = value;
                }
            });
        }

        function showNotification(message, type) {
            const notification = document.getElementById('notification');
            notification.textContent = message;
            notification.className = 'notification ' + type + ' show';
            
            setTimeout(function() {
                notification.classList.remove('show');
            }, 3000);
        }

        function toggleViewMode() {
            showNotification('View mode toggled', 'info');
        }

        function resetCamera() {
            controls.reset();
            showNotification('Camera reset', 'info');
        }

        function downloadData() {
            const dataStr = JSON.stringify(devices, null, 2);
            const dataBlob = new Blob([dataStr], { type: 'application/json' });
            const url = URL.createObjectURL(dataBlob);
            const link = document.createElement('a');
            link.href = url;
            link.download = 'sensor-data-' + new Date().toISOString() + '.json';
            link.click();
            URL.revokeObjectURL(url);
        }

        // Handle window resize
        window.addEventListener('resize', function() {
            if (camera && renderer) {
                const canvas = document.getElementById('visualizationCanvas');
                camera.aspect = canvas.offsetWidth / canvas.offsetHeight;
                camera.updateProjectionMatrix();
                renderer.setSize(canvas.offsetWidth, canvas.offsetHeight);
            }
        });

        // Initialize everything when page loads
        window.onload = function() {
            connectWebSocket();
            init3DVisualization();
            initMovementChart();
            
            // Request initial data
            setTimeout(function() {
                sendCommand('GET_ALL_DATA');
                sendCommand('GET_SERVER_STATUS');
            }, 1000);
        };
    </script>
</body>
</html>
    )=====";
}

// Главная страница
void handleRoot() {
  String html = "";
  html += getHTMLHeader();
  html += getHeaderSection();
  html += getDevicesSidebar();
  html += getVisualizationSection();
  html += getControlsSidebar();
  html += getNotificationSection();
  html += getJavaScriptCode();
  
  server.send(200, "text/html", html);
}

// API endpoint для получения данных в формате JSON
void handleApiData() {
  addCORSHeaders();
  
  String jsonData = "[";
  bool firstDevice = true;
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      if (!firstDevice) {
        jsonData += ",";
      }
      jsonData += "{";
      jsonData += "\"deviceId\":\"" + clients[i].deviceId + "\",";
      jsonData += "\"pitch\":" + String(clients[i].pitch, 2) + ",";
      jsonData += "\"roll\":" + String(clients[i].roll, 2) + ",";
      jsonData += "\"yaw\":" + String(clients[i].yaw, 2) + ",";
      jsonData += "\"relPitch\":" + String(clients[i].relPitch, 2) + ",";
      jsonData += "\"relRoll\":" + String(clients[i].relRoll, 2) + ",";
      jsonData += "\"relYaw\":" + String(clients[i].relYaw, 2) + ",";
      jsonData += "\"accPitch\":" + String(clients[i].accPitch, 2) + ",";
      jsonData += "\"accRoll\":" + String(clients[i].accRoll, 2) + ",";
      jsonData += "\"accYaw\":" + String(clients[i].accYaw, 2) + ",";
      jsonData += "\"zeroSet\":" + String(clients[i].zeroSet ? "true" : "false") + ",";
      jsonData += "\"zeroPitch\":" + String(clients[i].zeroPitch, 2) + ",";
      jsonData += "\"zeroRoll\":" + String(clients[i].zeroRoll, 2) + ",";
      jsonData += "\"zeroYaw\":" + String(clients[i].zeroYaw, 2) + ",";
      jsonData += "\"temperature\":" + String(clients[i].temperature, 1) + ",";
      jsonData += "\"batteryLevel\":" + String(clients[i].batteryLevel) + ",";
      jsonData += "\"signalStrength\":" + String(clients[i].signalStrength) + ",";
      jsonData += "\"isCalibrating\":" + String(clients[i].isCalibrating ? "true" : "false") + ",";
      jsonData += "\"deviceType\":\"" + clients[i].deviceType + "\",";
      jsonData += "\"firmwareVersion\":\"" + clients[i].firmwareVersion + "\",";
      jsonData += "\"dataStreamActive\":" + String(clients[i].dataStreamActive ? "true" : "false") + ",";
      jsonData += "\"sendMode\":\"" + clients[i].sendMode + "\",";
      jsonData += "\"movementMagnitude\":" + String(clients[i].movementMagnitude, 2) + ",";
      jsonData += "\"packetCount\":" + String(clients[i].packetCount) + ",";
      jsonData += "\"accX\":" + String(clients[i].accX, 2) + ",";
      jsonData += "\"accY\":" + String(clients[i].accY, 2) + ",";
      jsonData += "\"accZ\":" + String(clients[i].accZ, 2) + ",";
      jsonData += "\"gyroX\":" + String(clients[i].gyroX, 2) + ",";
      jsonData += "\"gyroY\":" + String(clients[i].gyroY, 2) + ",";
      jsonData += "\"gyroZ\":" + String(clients[i].gyroZ, 2) + ",";
      jsonData += "\"lastUpdate\":" + String(clients[i].lastUpdate);
      jsonData += "}";
      firstDevice = false;
    }
  }
  jsonData += "]";
  
  server.send(200, "application/json", jsonData);
}

// API endpoint для получения статуса сервера
void handleApiStatus() {
  addCORSHeaders();
  
  String jsonData = "{";
  jsonData += "\"status\":\"running\",";
  jsonData += "\"uptime\":" + String(millis() - serverStats.startTime) + ",";
  jsonData += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  
  int connectedCount = 0;
  int streamingCount = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      connectedCount++;
      if (clients[i].dataStreamActive) streamingCount++;
    }
  }
  jsonData += "\"connectedClients\":" + String(connectedCount) + ",";
  jsonData += "\"streamingClients\":" + String(streamingCount) + ",";
  jsonData += "\"maxClients\":" + String(MAX_CLIENTS) + ",";
  jsonData += "\"dataRate\":" + String(serverStats.dataRate) + ",";
  jsonData += "\"totalPackets\":" + String(serverStats.totalPackets) + ",";
  jsonData += "\"sendMode\":\"Enhanced Visualization\"";
  jsonData += "}";
  
  server.send(200, "application/json", jsonData);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println("🚀 Starting VR Data Server - Enhanced Visualization...");
  
  // Инициализация статистики сервера
  serverStats.startTime = millis();
  serverStats.connectedClients = 0;
  serverStats.totalPackets = 0;
  serverStats.dataRate = 0;
  
  // Настройка WiFi в режиме точки доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  Serial.print("📡 Access Point Started: ");
  Serial.println(ap_ssid);
  Serial.print("📱 IP Address: ");
  Serial.println(WiFi.softAPIP());
  
  // Настройка маршрутов веб-сервера
  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  server.on("/api/status", handleApiStatus);
  server.onNotFound([]() {
    server.send(404, "text/plain", "404: Not Found");
  });
  
  // Обработка OPTIONS запросов для CORS
  server.on("/api/data", HTTP_OPTIONS, handleOptions);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  
  // Запуск веб-сервера
  server.begin();
  Serial.println("🌐 HTTP server started");
  
  // Запуск WebSocket сервера
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("🔌 WebSocket server started on port 81");
  
  Serial.println("✅ Enhanced VR Data Server is ready!");
  Serial.println("📊 Available endpoints:");
  Serial.println("   - http://" + WiFi.softAPIP().toString() + "/ (Enhanced Web Interface)");
  Serial.println("   - http://" + WiFi.softAPIP().toString() + "/api/data (JSON API)");
  Serial.println("   - http://" + WiFi.softAPIP().toString() + "/api/status (Status API)");
  Serial.println("   - ws://" + WiFi.softAPIP().toString() + ":81 (WebSocket)");
  Serial.println("🎯 Features: 3D Visualization, Real-time Charts, Enhanced Controls");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Очистка устаревших клиентов каждые 10 секунд
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 10000) {
    cleanupOldClients();
    lastCleanup = millis();
  }
  
  // Рассылка данных всем WebSocket клиентам каждые 100 мс
  static unsigned long lastBroadcast = 0;
  if (millis() - lastBroadcast > 100) {
    broadcastData();
    lastBroadcast = millis();
  }
}
