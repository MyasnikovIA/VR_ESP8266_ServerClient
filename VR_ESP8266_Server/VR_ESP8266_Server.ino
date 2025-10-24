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
  float zeroPitch = 0;    // ДОБАВЛЕНО: нулевые точки
  float zeroRoll = 0;     // ДОБАВЛЕНО: нулевые точки
  float zeroYaw = 0;      // ДОБАВЛЕНО: нулевые точки
  unsigned long lastUpdate = 0;
  bool connected = false;
  // ДОБАВЛЕНО: новые поля из клиента
  float temperature = 0;
  int batteryLevel = 100;
  int signalStrength = 0;
  bool isCalibrating = false;
  String deviceType = "MPU6050";
  String firmwareVersion = "1.0";
};

// Максимальное количество клиентов
const int MAX_CLIENTS = 5;
ClientData clients[MAX_CLIENTS];

// Время жизни данных (5 секунд)
const unsigned long DATA_TIMEOUT = 5000;

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

// Обновление данных клиента
void updateClientData(String deviceId, String data) {
  int clientIndex = findClientIndex(deviceId);
  
  // Если клиент не найден, пытаемся зарегистрировать нового
  if (clientIndex == -1) {
    clientIndex = findFreeClientSlot();
    if (clientIndex == -1) {
      Serial.println("No free slots for new client: " + deviceId);
      return;
    }
    clients[clientIndex].deviceId = deviceId;
    clients[clientIndex].connected = true;
    Serial.println("New client registered: " + deviceId);
  }
  
  // Парсим данные
  clients[clientIndex].lastUpdate = millis();
  
  // Парсим строку данных
  int start = 0;
  int end = data.indexOf(',');
  while (end != -1) {
    String pair = data.substring(start, end);
    int colon = pair.indexOf(':');
    if (colon != -1) {
      String key = pair.substring(0, colon);
      String value = pair.substring(colon + 1);
      
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
      // ДОБАВЛЕНО: парсинг нулевых точек
      else if (key == "ZERO_PITCH") clients[clientIndex].zeroPitch = value.toFloat();
      else if (key == "ZERO_ROLL") clients[clientIndex].zeroRoll = value.toFloat();
      else if (key == "ZERO_YAW") clients[clientIndex].zeroYaw = value.toFloat();
      // ДОБАВЛЕНО: парсинг новых данных из клиента
      else if (key == "TEMP") clients[clientIndex].temperature = value.toFloat();
      else if (key == "BATTERY") clients[clientIndex].batteryLevel = value.toInt();
      else if (key == "RSSI") clients[clientIndex].signalStrength = value.toInt();
      else if (key == "CALIBRATING") clients[clientIndex].isCalibrating = (value == "true");
      else if (key == "DEVICE_TYPE") clients[clientIndex].deviceType = value;
      else if (key == "FW_VERSION") clients[clientIndex].firmwareVersion = value;
    }
    
    start = end + 1;
    end = data.indexOf(',', start);
  }
  
  // Обрабатываем последнюю пару
  String pair = data.substring(start);
  int colon = pair.indexOf(':');
  if (colon != -1) {
    String key = pair.substring(0, colon);
    String value = pair.substring(colon + 1);
    
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
    // ДОБАВЛЕНО: парсинг нулевых точек
    else if (key == "ZERO_PITCH") clients[clientIndex].zeroPitch = value.toFloat();
    else if (key == "ZERO_ROLL") clients[clientIndex].zeroRoll = value.toFloat();
    else if (key == "ZERO_YAW") clients[clientIndex].zeroYaw = value.toFloat();
    // ДОБАВЛЕНО: парсинг новых данных из клиента
    else if (key == "TEMP") clients[clientIndex].temperature = value.toFloat();
    else if (key == "BATTERY") clients[clientIndex].batteryLevel = value.toInt();
    else if (key == "RSSI") clients[clientIndex].signalStrength = value.toInt();
    else if (key == "CALIBRATING") clients[clientIndex].isCalibrating = (value == "true");
    else if (key == "DEVICE_TYPE") clients[clientIndex].deviceType = value;
    else if (key == "FW_VERSION") clients[clientIndex].firmwareVersion = value;
  }
}

// Очистка устаревших клиентов
void cleanupOldClients() {
  unsigned long currentTime = millis();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected && (currentTime - clients[i].lastUpdate > DATA_TIMEOUT)) {
      Serial.println("Client timeout: " + clients[i].deviceId);
      clients[i].connected = false;
      clients[i].deviceId = "";
    }
  }
}

// Отправка данных всем WebSocket клиентам
void broadcastData() {
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
                   // ДОБАВЛЕНО: отправка нулевых точек
                   ",ZERO_PITCH:" + String(clients[i].zeroPitch, 2) +
                   ",ZERO_ROLL:" + String(clients[i].zeroRoll, 2) +
                   ",ZERO_YAW:" + String(clients[i].zeroYaw, 2) +
                   // ДОБАВЛЕНО: отправка новых данных
                   ",TEMP:" + String(clients[i].temperature, 1) +
                   ",BATTERY:" + String(clients[i].batteryLevel) +
                   ",RSSI:" + String(clients[i].signalStrength) +
                   ",CALIBRATING:" + String(clients[i].isCalibrating ? "true" : "false") +
                   ",DEVICE_TYPE:" + clients[i].deviceType +
                   ",FW_VERSION:" + clients[i].firmwareVersion;
      webSocket.broadcastTXT(data);
    }
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
        // Отправляем приветственное сообщение
        String welcomeMsg = "SERVER:Welcome to VR Data Server";
        webSocket.sendTXT(num, welcomeMsg);
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[%u] Received: %s\n", num, message);
        
        // Обработка данных от клиентов MPU6050
        if (message.startsWith("deviceId:")) {
          int commaPos = message.indexOf(',');
          if (commaPos != -1) {
            String deviceId = message.substring(9, commaPos);
            String data = message.substring(commaPos + 1);
            updateClientData(deviceId, data);
            
            // Подтверждение получения
            String ackMsg = "DATA_RECEIVED:" + deviceId;
            webSocket.sendTXT(num, ackMsg);
          }
        }
        // Обработка команд от веб-клиентов
        else if (message.startsWith("CMD:")) {
          String command = message.substring(4);
          
          if (command == "GET_ALL_DATA") {
            broadcastData();
          }
          else if (command.startsWith("SEND_TO_DEVICE:")) {
            String deviceCmd = command.substring(15);
            int colonPos = deviceCmd.indexOf(':');
            if (colonPos != -1) {
              String targetDevice = deviceCmd.substring(0, colonPos);
              String cmd = deviceCmd.substring(colonPos + 1);
              
              // Отправка команды конкретному устройству
              Serial.println("Command for " + targetDevice + ": " + cmd);
              
              // Ищем клиента с таким deviceId
              int clientIndex = findClientIndex(targetDevice);
              if (clientIndex != -1 && clients[clientIndex].connected) {
                // Отправляем команду устройству
                webSocket.sendTXT(num, cmd);
                Serial.println("Command sent to device: " + cmd);
                
                // Подтверждение отправки команды
                String confirmMsg = "COMMAND_SENT:" + targetDevice + ":" + cmd;
                webSocket.sendTXT(num, confirmMsg);
              } else {
                String errorMsg = "DEVICE_NOT_FOUND:" + targetDevice;
                webSocket.sendTXT(num, errorMsg);
              }
            }
          }
          // Обработка широковещательных команд
          else if (command.startsWith("BROADCAST:")) {
            String broadcastCmd = command.substring(10);
            Serial.println("Broadcast command: " + broadcastCmd);
            
            // Отправляем команду всем подключенным клиентам MPU6050
            for (int i = 0; i < MAX_CLIENTS; i++) {
              if (clients[i].connected) {
                webSocket.sendTXT(num, broadcastCmd);
              }
            }
            
            String confirmMsg = "BROADCAST_SENT:" + broadcastCmd;
            webSocket.sendTXT(num, confirmMsg);
          }
          // ДОБАВЛЕНО: Обработка новых команд
          else if (command == "GET_DEVICE_LIST") {
            // Отправляем список всех подключенных устройств
            String deviceList = "DEVICE_LIST:";
            for (int i = 0; i < MAX_CLIENTS; i++) {
              if (clients[i].connected) {
                if (deviceList != "DEVICE_LIST:") {
                  deviceList += ",";
                }
                deviceList += clients[i].deviceId;
              }
            }
            webSocket.sendTXT(num, deviceList);
          }
          else if (command == "CLEAR_INACTIVE_DEVICES") {
            // Очистка неактивных устройств
            int clearedCount = 0;
            unsigned long currentTime = millis();
            for (int i = 0; i < MAX_CLIENTS; i++) {
              if (clients[i].connected && (currentTime - clients[i].lastUpdate > DATA_TIMEOUT)) {
                Serial.println("Clearing inactive device: " + clients[i].deviceId);
                clients[i].connected = false;
                clients[i].deviceId = "";
                clearedCount++;
              }
            }
            String clearMsg = "CLEARED_DEVICES:" + String(clearedCount);
            webSocket.sendTXT(num, clearMsg);
          }
          else if (command == "GET_SERVER_STATUS") {
            // Статус сервера
            int connectedCount = 0;
            for (int i = 0; i < MAX_CLIENTS; i++) {
              if (clients[i].connected) connectedCount++;
            }
            String statusMsg = "SERVER_STATUS:Uptime:" + String(millis()) + 
                              ",ConnectedDevices:" + String(connectedCount) +
                              ",FreeMemory:" + String(ESP.getFreeHeap());
            webSocket.sendTXT(num, statusMsg);
          }
          else if (command.startsWith("SET_UPDATE_RATE:")) {
            // Установка частоты обновления (для информационных целей)
            String rate = command.substring(16);
            String rateMsg = "UPDATE_RATE_SET:" + rate + "ms";
            webSocket.sendTXT(num, rateMsg);
            Serial.println("Update rate info: " + rate + "ms");
          }
          // ДОБАВЛЕНО: Новые команды управления
          else if (command == "START_DATA_STREAM") {
            webSocket.broadcastTXT("START_STREAM");
            String response = "DATA_STREAM_STARTED";
            webSocket.sendTXT(num, response);
          }
          else if (command == "STOP_DATA_STREAM") {
            webSocket.broadcastTXT("STOP_STREAM");
            String response = "DATA_STREAM_STOPPED";
            webSocket.sendTXT(num, response);
          }
          else if (command == "FORCE_CALIBRATION") {
            webSocket.broadcastTXT("FORCE_CALIBRATE");
            String response = "FORCE_CALIBRATION_SENT";
            webSocket.sendTXT(num, response);
          }
          else if (command == "GET_DEVICE_INFO_ALL") {
            webSocket.broadcastTXT("GET_INFO");
            String response = "DEVICE_INFO_REQUEST_SENT";
            webSocket.sendTXT(num, response);
          }
        }
        // Обработка статусных сообщений от клиентов MPU6050
        else if (message.startsWith("CLIENT_ID:") || 
                 message.startsWith("RECALIBRATION_COMPLETE:") ||
                 message.startsWith("ANGLES_RESET:") ||
                 message.startsWith("ZERO_POINT_SET:") ||
                 message.startsWith("ZERO_POINT_RESET:") ||
                 message.startsWith("FORCE_CALIBRATION_COMPLETE:") ||
                 message.startsWith("CURRENT_POSITION_SET_AS_ZERO:") ||
                 message.startsWith("ZERO_SET_AT_CURRENT:") ||
                 // ДОБАВЛЕНО: новые статусные сообщения
                 message.startsWith("CALIBRATION_STARTED:") ||
                 message.startsWith("DATA_SENDING_STARTED:") ||
                 message.startsWith("DATA_SENDING_STOPPED:") ||
                 message.startsWith("DEVICE_READY:") ||
                 message.startsWith("LOW_BATTERY:") ||
                 message.startsWith("SENSOR_ERROR:") ||
                 message.startsWith("STREAM_STARTED:") ||
                 message.startsWith("STREAM_STOPPED:") ||
                 message.startsWith("CALIBRATION_PROGRESS:")) {
          // Пересылаем статусные сообщения всем веб-клиентам
          webSocket.broadcastTXT(message);
          Serial.println("Status message broadcasted: " + message);
        }
        // ДОБАВЛЕНО: Обработка запросов информации от устройств
        else if (message.startsWith("DEVICE_INFO:")) {
          // Пересылаем информацию об устройстве всем веб-клиентам
          webSocket.broadcastTXT(message);
          Serial.println("Device info broadcasted: " + message);
        }
        // ДОБАВЛЕНО: Обработка данных потока от клиентов
        else if (message.startsWith("DATA_STREAM:")) {
          // Пересылаем потоковые данные всем веб-клиентам
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

// Главная страница
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>VR Data Server</title>";
  html += "<meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f5f5f5; }";
  html += ".container { max-width: 1200px; margin: 0 auto; }";
  html += ".header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 30px; border-radius: 15px; margin-bottom: 20px; }";
  html += ".clients-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(400px, 1fr)); gap: 20px; margin-bottom: 20px; }";
  html += ".client-card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += ".client-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; padding-bottom: 10px; border-bottom: 2px solid #f0f0f0; }";
  html += ".client-id { font-size: 18px; font-weight: bold; color: #333; }";
  html += ".client-status { padding: 5px 10px; border-radius: 15px; font-size: 12px; font-weight: bold; }";
  html += ".status-connected { background: #d4edda; color: #155724; }";
  html += ".status-disconnected { background: #f8d7da; color: #721c24; }";
  html += ".data-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }";
  html += ".data-item { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #f0f0f0; }";
  html += ".data-label { font-weight: bold; color: #666; }";
  html += ".data-value { font-weight: bold; color: #333; }";
  html += ".device-info { background: #f8f9fa; padding: 15px; border-radius: 8px; margin: 10px 0; }";
  html += ".controls { background: #e8f5e8; padding: 20px; border-radius: 10px; margin: 20px 0; }";
  html += "button { padding: 10px 15px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; }";
  html += ".btn-primary { background: #007bff; color: white; }";
  html += ".btn-success { background: #28a745; color: white; }";
  html += ".btn-warning { background: #ffc107; color: black; }";
  html += ".btn-danger { background: #dc3545; color: white; }";
  html += ".btn-info { background: #17a2b8; color: white; }";
  html += ".btn-secondary { background: #6c757d; color: white; }";
  html += ".server-info { background: white; padding: 20px; border-radius: 10px; margin-bottom: 20px; }";
  html += ".connection-status { padding: 15px; border-radius: 8px; margin-bottom: 20px; font-weight: bold; }";
  html += ".connected { background: #d4edda; color: #155724; border: 2px solid #c3e6cb; }";
  html += ".disconnected { background: #f8d7da; color: #721c24; border: 2px solid #f5c6cb; }";
  html += ".advanced-controls { background: #e3f2fd; padding: 20px; border-radius: 10px; margin: 20px 0; }";
  html += ".stream-controls { background: #fff3cd; padding: 20px; border-radius: 10px; margin: 20px 0; }";
  html += ".battery-low { color: #dc3545; font-weight: bold; }";
  html += ".battery-medium { color: #ffc107; font-weight: bold; }";
  html += ".battery-high { color: #28a745; font-weight: bold; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"container\">";
  html += "<div class=\"header\">";
  html += "<h1>🚀 VR Data Server</h1>";
  html += "<p>Real-time MPU6050 sensor data from connected devices</p>";
  html += "<div class=\"connection-status\" id=\"connectionStatus\">WebSocket: Connecting...</div>";
  html += "</div>";
  html += "<div class=\"server-info\">";
  html += "<h3>📡 Server Information</h3>";
  html += "<p><strong>SSID:</strong> ESP8266_AP</p>";
  html += "<p><strong>IP Address:</strong> 192.168.4.1</p>";
  html += "<p><strong>WebSocket Port:</strong> 81</p>";
  html += "<p><strong>Connected Devices:</strong> <span id=\"connectedCount\">0</span></p>";
  html += "<p><strong>Server Uptime:</strong> <span id=\"serverUptime\">0</span> ms</p>";
  html += "</div>";
  html += "<div class=\"stream-controls\">";
  html += "<h3>📊 Data Stream Controls</h3>";
  html += "<button class=\"btn-success\" onclick=\"sendCommand('START_DATA_STREAM')\">Start Data Stream</button>";
  html += "<button class=\"btn-danger\" onclick=\"sendCommand('STOP_DATA_STREAM')\">Stop Data Stream</button>";
  html += "<button class=\"btn-warning\" onclick=\"sendCommand('FORCE_CALIBRATION')\">Force Calibration All</button>";
  html += "<button class=\"btn-info\" onclick=\"sendCommand('GET_DEVICE_INFO_ALL')\">Get All Device Info</button>";
  html += "</div>";
  html += "<div class=\"controls\">";
  html += "<h3>🛠️ Server Controls</h3>";
  html += "<button class=\"btn-primary\" onclick=\"sendCommand('GET_ALL_DATA')\">Refresh All Data</button>";
  html += "<button class=\"btn-success\" onclick=\"sendCommand('BROADCAST:GET_DATA')\">Request Data from Devices</button>";
  html += "<button class=\"btn-warning\" onclick=\"sendCommand('BROADCAST:RECALIBRATE')\">Recalibrate All Devices</button>";
  html += "<button class=\"btn-danger\" onclick=\"sendCommand('BROADCAST:RESET_ANGLES')\">Reset All Angles</button>";
  html += "<button class=\"btn-success\" onclick=\"sendCommand('BROADCAST:SET_CURRENT_AS_ZERO')\">Fix Current Position as Zero for All</button>";
  html += "</div>";
  html += "<div class=\"advanced-controls\">";
  html += "<h3>⚙️ Advanced Controls</h3>";
  html += "<button class=\"btn-info\" onclick=\"sendCommand('GET_DEVICE_LIST')\">Get Device List</button>";
  html += "<button class=\"btn-info\" onclick=\"sendCommand('GET_SERVER_STATUS')\">Get Server Status</button>";
  html += "<button class=\"btn-warning\" onclick=\"sendCommand('CLEAR_INACTIVE_DEVICES')\">Clear Inactive Devices</button>";
  html += "<button class=\"btn-info\" onclick=\"sendCommand('SET_UPDATE_RATE:100')\">Set Update Rate: 100ms</button>";
  html += "<button class=\"btn-info\" onclick=\"sendCommand('SET_UPDATE_RATE:500')\">Set Update Rate: 500ms</button>";
  html += "</div>";
  html += "<div id=\"clientsContainer\" class=\"clients-grid\"></div>";
  html += "<div class=\"server-info\">";
  html += "<h3>📊 Last Message</h3>";
  html += "<div id=\"lastMessage\" style=\"background: #f8f9fa; padding: 15px; border-radius: 5px; font-family: monospace; min-height: 20px; font-size: 12px;\">No messages yet</div>";
  html += "</div>";
  html += "</div>";
  html += "<script>";
  html += "let ws = null;";
  html += "let clients = {};";
  html += "function connectWebSocket() {";
  html += "const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';";
  html += "const wsUrl = protocol + '//' + window.location.hostname + ':81';";
  html += "ws = new WebSocket(wsUrl);";
  html += "ws.onopen = function() {";
  html += "console.log('WebSocket connected to server');";
  html += "document.getElementById('connectionStatus').textContent = 'WebSocket: Connected';";
  html += "document.getElementById('connectionStatus').className = 'connection-status connected';";
  html += "};";
  html += "ws.onmessage = function(event) {";
  html += "console.log('Received:', event.data);";
  html += "document.getElementById('lastMessage').textContent = event.data;";
  html += "if (event.data.startsWith('CLIENT:')) {";
  html += "updateClientData(event.data);";
  html += "}";
  html += "else if (event.data.startsWith('SERVER:')) {";
  html += "showNotification(event.data.substring(7), 'info');";
  html += "}";
  html += "else if (event.data.startsWith('DATA_RECEIVED:')) {";
  html += "showNotification('Data received from: ' + event.data.substring(14), 'success');";
  html += "}";
  html += "else if (event.data.startsWith('CURRENT_POSITION_SET_AS_ZERO:')) {";
  html += "showNotification('Current position set as zero for: ' + event.data.substring(28), 'success');";
  html += "}";
  html += "else if (event.data.startsWith('ZERO_SET_AT_CURRENT:')) {";
  html += "showNotification('Zero point set at current position for device', 'success');";
  html += "}";
  html += "// ДОБАВЛЕНО: обработка новых сообщений";
  html += "else if (event.data.startsWith('DEVICE_LIST:')) {";
  html += "showNotification('Device list: ' + event.data.substring(12), 'info');";
  html += "}";
  html += "else if (event.data.startsWith('SERVER_STATUS:')) {";
  html += "const status = event.data.substring(14);";
  html += "const parts = status.split(',');";
  html += "parts.forEach(part => {";
  html += "if (part.startsWith('Uptime:')) {";
  html += "document.getElementById('serverUptime').textContent = part.substring(7);";
  html += "}";
  html += "});";
  html += "showNotification('Server status updated', 'info');";
  html += "}";
  html += "else if (event.data.startsWith('CLEARED_DEVICES:')) {";
  html += "showNotification('Cleared ' + event.data.substring(16) + ' inactive devices', 'warning');";
  html += "}";
  html += "else if (event.data.startsWith('UPDATE_RATE_SET:')) {";
  html += "showNotification('Update rate set to: ' + event.data.substring(16), 'info');";
  html += "}";
  html += "else if (event.data.startsWith('CALIBRATION_STARTED:')) {";
  html += "showNotification('Calibration started for: ' + event.data.substring(20), 'info');";
  html += "}";
  html += "else if (event.data.startsWith('DEVICE_INFO:')) {";
  html += "showNotification('Device info: ' + event.data.substring(12), 'info');";
  html += "}";
  html += "else if (event.data.startsWith('LOW_BATTERY:')) {";
  html += "showNotification('Low battery warning: ' + event.data.substring(12), 'error');";
  html += "}";
  html += "else if (event.data.startsWith('DATA_STREAM_STARTED')) {";
  html += "showNotification('Data stream started on all devices', 'success');";
  html += "}";
  html += "else if (event.data.startsWith('DATA_STREAM_STOPPED')) {";
  html += "showNotification('Data stream stopped on all devices', 'warning');";
  html += "}";
  html += "else if (event.data.startsWith('FORCE_CALIBRATION_SENT')) {";
  html += "showNotification('Force calibration sent to all devices', 'info');";
  html += "}";
  html += "};";
  html += "ws.onclose = function() {";
  html += "console.log('WebSocket disconnected');";
  html += "document.getElementById('connectionStatus').textContent = 'WebSocket: Disconnected';";
  html += "document.getElementById('connectionStatus').className = 'connection-status disconnected';";
  html += "setTimeout(connectWebSocket, 2000);";
  html += "};";
  html += "ws.onerror = function(error) {";
  html += "console.error('WebSocket error:', error);";
  html += "};";
  html += "}";
  html += "function updateClientData(data) {";
  html += "const parts = data.split(',');";
  html += "const clientId = parts[0].substring(7);";
  html += "if (!clients[clientId]) {";
  html += "clients[clientId] = {};";
  html += "createClientCard(clientId);";
  html += "}";
  html += "parts.forEach(part => {";
  html += "const [key, value] = part.split(':');";
  html += "clients[clientId][key] = value;";
  html += "});";
  html += "updateClientDisplay(clientId);";
  html += "updateConnectedCount();";
  html += "}";
  html += "function createClientCard(clientId) {";
  html += "const container = document.getElementById('clientsContainer');";
  html += "const card = document.createElement('div');";
  html += "card.className = 'client-card';";
  html += "card.id = 'card-' + clientId;";
  html += "card.innerHTML = `";
  html += "<div class=\\\"client-header\\\">";
  html += "<div class=\\\"client-id\\\">${clientId}</div>";
  html += "<div class=\\\"client-status status-connected\\\" id=\\\"status-${clientId}\\\">Connected</div>";
  html += "</div>";
  html += "<div class=\\\"device-info\\\">";
  html += "<div style=\\\"display: flex; justify-content: space-between; font-size: 12px;\\\">";
  html += "<span><strong>Type:</strong> <span id=\\\"deviceType-${clientId}\\\">MPU6050</span></span>";
  html += "<span><strong>Firmware:</strong> <span id=\\\"fwVersion-${clientId}\\\">1.0</span></span>";
  html += "</div>";
  html += "<div style=\\\"display: flex; justify-content: space-between; font-size: 12px; margin-top: 5px;\\\">";
  html += "<span><strong>Battery:</strong> <span id=\\\"battery-${clientId}\\\">100%</span></span>";
  html += "<span><strong>Temp:</strong> <span id=\\\"temp-${clientId}\\\">0°C</span></span>";
  html += "<span><strong>Signal:</strong> <span id=\\\"signal-${clientId}\\\">0 dBm</span></span>";
  html += "</div>";
  html += "</div>";
  html += "<div class=\\\"data-grid\\\">";
  html += "<div><strong>Absolute Angles</strong></div><div><strong>Relative Angles</strong></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Pitch:</span><span class=\\\"data-value\\\" id=\\\"pitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Rel Pitch:</span><span class=\\\"data-value\\\" id=\\\"relPitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Roll:</span><span class=\\\"data-value\\\" id=\\\"roll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Rel Roll:</span><span class=\\\"data-value\\\" id=\\\"relRoll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Yaw:</span><span class=\\\"data-value\\\" id=\\\"yaw-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Rel Yaw:</span><span class=\\\"data-value\\\" id=\\\"relYaw-${clientId}\\\">0°</span></div>";
  html += "</div>";
  html += "<div style=\\\"margin-top: 15px; padding-top: 10px; border-top: 1px solid #f0f0f0;\\\">";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Set:</span><span class=\\\"data-value\\\" id=\\\"zeroSet-${clientId}\\\">false</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Pitch:</span><span class=\\\"data-value\\\" id=\\\"zeroPitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Roll:</span><span class=\\\"data-value\\\" id=\\\"zeroRoll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Yaw:</span><span class=\\\"data-value\\\" id=\\\"zeroYaw-${clientId}\\\">0°</span></div>";
  html += "</div>";
  html += "<div style=\\\"margin-top: 15px; display: flex; flex-wrap: wrap; gap: 5px;\\\">";
  html += "<button class=\\\"btn-success\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'RECALIBRATE')\\\">Recalibrate</button>";
  html += "<button class=\\\"btn-warning\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'RESET_ANGLES')\\\">Reset Angles</button>";
  html += "<button class=\\\"btn-info\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'SET_CURRENT_AS_ZERO')\\\">Set Current as Zero</button>";
  html += "<button class=\\\"btn-danger\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'RESET_ZERO_POINT')\\\">Reset Zero</button>";
  html += "</div>";
  html += "`;";
  html += "container.appendChild(card);";
  html += "}";
  html += "function updateClientDisplay(clientId) {";
  html += "const client = clients[clientId];";
  html += "if (!client) return;";
  html += "document.getElementById('pitch-' + clientId).textContent = client.PITCH + '°';";
  html += "document.getElementById('roll-' + clientId).textContent = client.ROLL + '°';";
  html += "document.getElementById('yaw-' + clientId).textContent = client.YAW + '°';";
  html += "document.getElementById('relPitch-' + clientId).textContent = client.REL_PITCH + '°';";
  html += "document.getElementById('relRoll-' + clientId).textContent = client.REL_ROLL + '°';";
  html += "document.getElementById('relYaw-' + clientId).textContent = client.REL_YAW + '°';";
  html += "document.getElementById('zeroSet-' + clientId).textContent = client.ZERO_SET;";
  html += "document.getElementById('zeroPitch-' + clientId).textContent = client.ZERO_PITCH + '°';";
  html += "document.getElementById('zeroRoll-' + clientId).textContent = client.ZERO_ROLL + '°';";
  html += "document.getElementById('zeroYaw-' + clientId).textContent = client.ZERO_YAW + '°';";
  html += "// ДОБАВЛЕНО: обновление новых полей";
  html += "if (client.DEVICE_TYPE) document.getElementById('deviceType-' + clientId).textContent = client.DEVICE_TYPE;";
  html += "if (client.FW_VERSION) document.getElementById('fwVersion-' + clientId).textContent = client.FW_VERSION;";
  html += "if (client.BATTERY) {";
  html += "const battery = parseInt(client.BATTERY);";
  html += "const batteryElement = document.getElementById('battery-' + clientId);";
  html += "batteryElement.textContent = battery + '%';";
  html += "if (battery < 20) batteryElement.className = 'battery-low';";
  html += "else if (battery < 50) batteryElement.className = 'battery-medium';";
  html += "else batteryElement.className = 'battery-high';";
  html += "}";
  html += "if (client.TEMP) document.getElementById('temp-' + clientId).textContent = client.TEMP + '°C';";
  html += "if (client.RSSI) document.getElementById('signal-' + clientId).textContent = client.RSSI + ' dBm';";
  html += "}";
  html += "function updateConnectedCount() {";
  html += "const count = Object.keys(clients).length;";
  html += "document.getElementById('connectedCount').textContent = count;";
  html += "}";
  html += "function sendCommand(command) {";
  html += "if (ws && ws.readyState === WebSocket.OPEN) {";
  html += "ws.send('CMD:' + command);";
  html += "console.log('Sent command:', command);";
  html += "} else {";
  html += "console.error('WebSocket not connected');";
  html += "}";
  html += "}";
  html += "function sendDeviceCommand(deviceId, command) {";
  html += "if (ws && ws.readyState === WebSocket.OPEN) {";
  html += "ws.send('CMD:SEND_TO_DEVICE:' + deviceId + ':' + command);";
  html += "console.log('Sent command to device:', deviceId, command);";
  html += "} else {";
  html += "console.error('WebSocket not connected');";
  html += "}";
  html += "}";
  html += "function showNotification(message, type) {";
  html += "console.log('Notification:', message, type);";
  html += "}";
  html += "connectWebSocket();";
  html += "setInterval(() => {";
  html += "sendCommand('GET_ALL_DATA');";
  html += "}, 1000);";
  html += "</script>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// API endpoint для получения данных в формате JSON
void handleApiData() {
  addCORSHeaders();
  
  DynamicJsonDocument doc(4096);
  JsonArray clientsArray = doc.to<JsonArray>();
  
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) {
      JsonObject client = clientsArray.createNestedObject();
      client["deviceId"] = clients[i].deviceId;
      client["pitch"] = clients[i].pitch;
      client["roll"] = clients[i].roll;
      client["yaw"] = clients[i].yaw;
      client["relPitch"] = clients[i].relPitch;
      client["relRoll"] = clients[i].relRoll;
      client["relYaw"] = clients[i].relYaw;
      client["accPitch"] = clients[i].accPitch;
      client["accRoll"] = clients[i].accRoll;
      client["accYaw"] = clients[i].accYaw;
      client["zeroSet"] = clients[i].zeroSet;
      // ДОБАВЛЕНО: нулевые точки
      client["zeroPitch"] = clients[i].zeroPitch;
      client["zeroRoll"] = clients[i].zeroRoll;
      client["zeroYaw"] = clients[i].zeroYaw;
      // ДОБАВЛЕНО: новые поля
      client["temperature"] = clients[i].temperature;
      client["batteryLevel"] = clients[i].batteryLevel;
      client["signalStrength"] = clients[i].signalStrength;
      client["isCalibrating"] = clients[i].isCalibrating;
      client["deviceType"] = clients[i].deviceType;
      client["firmwareVersion"] = clients[i].firmwareVersion;
      client["lastUpdate"] = clients[i].lastUpdate;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API endpoint для получения статуса сервера
void handleApiStatus() {
  addCORSHeaders();
  
  DynamicJsonDocument doc(512);
  doc["status"] = "running";
  doc["uptime"] = millis();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["connectedClients"] = 0;
  
  int connectedCount = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected) connectedCount++;
  }
  doc["connectedClients"] = connectedCount;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println("🚀 Starting VR Data Server...");
  
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
  
  Serial.println("✅ VR Data Server is ready!");
  Serial.println("📊 Available endpoints:");
  Serial.println("   - http://" + WiFi.softAPIP().toString() + "/ (Web Interface)");
  Serial.println("   - http://" + WiFi.softAPIP().toString() + "/api/data (JSON API)");
  Serial.println("   - http://" + WiFi.softAPIP().toString() + "/api/status (Status API)");
  Serial.println("   - ws://" + WiFi.softAPIP().toString() + ":81 (WebSocket)");
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
