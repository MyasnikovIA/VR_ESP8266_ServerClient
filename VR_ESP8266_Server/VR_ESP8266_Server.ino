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
                   ",ZERO_YAW:" + String(clients[i].zeroYaw, 2);
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
        }
        // Обработка статусных сообщений от клиентов MPU6050
        else if (message.startsWith("CLIENT_ID:") || 
                 message.startsWith("RECALIBRATION_COMPLETE:") ||
                 message.startsWith("ANGLES_RESET:") ||
                 message.startsWith("ZERO_POINT_SET:") ||
                 message.startsWith("ZERO_POINT_RESET:") ||
                 message.startsWith("FORCE_CALIBRATION_COMPLETE:") ||
                 message.startsWith("CURRENT_POSITION_SET_AS_ZERO:") ||
                 message.startsWith("ZERO_SET_AT_CURRENT:")) {
          // Пересылаем статусные сообщения всем веб-клиентам
          webSocket.broadcastTXT(message);
          Serial.println("Status message broadcasted: " + message);
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
  html += ".clients-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 20px; margin-bottom: 20px; }";
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
  html += ".controls { background: #e8f5e8; padding: 20px; border-radius: 10px; margin: 20px 0; }";
  html += "button { padding: 10px 15px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; }";
  html += ".btn-primary { background: #007bff; color: white; }";
  html += ".btn-success { background: #28a745; color: white; }";
  html += ".btn-warning { background: #ffc107; color: black; }";
  html += ".btn-danger { background: #dc3545; color: white; }";
  html += ".server-info { background: white; padding: 20px; border-radius: 10px; margin-bottom: 20px; }";
  html += ".connection-status { padding: 15px; border-radius: 8px; margin-bottom: 20px; font-weight: bold; }";
  html += ".connected { background: #d4edda; color: #155724; border: 2px solid #c3e6cb; }";
  html += ".disconnected { background: #f8d7da; color: #721c24; border: 2px solid #f5c6cb; }";
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
  html += "</div>";
  html += "<div class=\"controls\">";
  html += "<h3>🛠️ Server Controls</h3>";
  html += "<button class=\"btn-primary\" onclick=\"sendCommand('GET_ALL_DATA')\">Refresh All Data</button>";
  html += "<button class=\"btn-success\" onclick=\"sendCommand('BROADCAST:GET_DATA')\">Request Data from Devices</button>";
  html += "<button class=\"btn-warning\" onclick=\"sendCommand('BROADCAST:RECALIBRATE')\">Recalibrate All Devices</button>";
  html += "<button class=\"btn-danger\" onclick=\"sendCommand('BROADCAST:RESET_ANGLES')\">Reset All Angles</button>";
  html += "<button class=\"btn-success\" onclick=\"sendCommand('BROADCAST:SET_CURRENT_AS_ZERO')\">Fix Current Position as Zero for All</button>";
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
  html += "<div class=\\\"data-grid\\\">";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Pitch:</span><span class=\\\"data-value\\\" id=\\\"pitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Roll:</span><span class=\\\"data-value\\\" id=\\\"roll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Yaw:</span><span class=\\\"data-value\\\" id=\\\"yaw-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Rel Pitch:</span><span class=\\\"data-value\\\" id=\\\"relPitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Rel Roll:</span><span class=\\\"data-value\\\" id=\\\"relRoll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Rel Yaw:</span><span class=\\\"data-value\\\" id=\\\"relYaw-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Acc Pitch:</span><span class=\\\"data-value\\\" id=\\\"accPitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Acc Roll:</span><span class=\\\"data-value\\\" id=\\\"accRoll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Acc Yaw:</span><span class=\\\"data-value\\\" id=\\\"accYaw-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Set:</span><span class=\\\"data-value\\\" id=\\\"zeroSet-${clientId}\\\">false</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Pitch:</span><span class=\\\"data-value\\\" id=\\\"zeroPitch-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Roll:</span><span class=\\\"data-value\\\" id=\\\"zeroRoll-${clientId}\\\">0°</span></div>";
  html += "<div class=\\\"data-item\\\"><span class=\\\"data-label\\\">Zero Yaw:</span><span class=\\\"data-value\\\" id=\\\"zeroYaw-${clientId}\\\">0°</span></div>";
  html += "</div>";
  html += "<div style=\\\"margin-top: 15px;\\\">";
  html += "<button class=\\\"btn-primary\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'GET_DATA')\\\">Get Data</button>";
  html += "<button class=\\\"btn-warning\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'RECALIBRATE')\\\">Recalibrate</button>";
  html += "<button class=\\\"btn-success\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'SET_CURRENT_AS_ZERO')\\\">Fix Current Position as Zero</button>";
  html += "<button class=\\\"btn-danger\\\" onclick=\\\"sendDeviceCommand('${clientId}', 'RESET_ZERO')\\\">Reset Zero</button>";
  html += "</div>";
  html += "`;";
  html += "container.appendChild(card);";
  html += "}";
  html += "function updateClientDisplay(clientId) {";
  html += "const client = clients[clientId];";
  html += "if (!client) return;";
  html += "document.getElementById(`pitch-${clientId}`).textContent = client.PITCH + '°';";
  html += "document.getElementById(`roll-${clientId}`).textContent = client.ROLL + '°';";
  html += "document.getElementById(`yaw-${clientId}`).textContent = client.YAW + '°';";
  html += "document.getElementById(`relPitch-${clientId}`).textContent = client.REL_PITCH + '°';";
  html += "document.getElementById(`relRoll-${clientId}`).textContent = client.REL_ROLL + '°';";
  html += "document.getElementById(`relYaw-${clientId}`).textContent = client.REL_YAW + '°';";
  html += "document.getElementById(`accPitch-${clientId}`).textContent = client.ACC_PITCH + '°';";
  html += "document.getElementById(`accRoll-${clientId}`).textContent = client.ACC_ROLL + '°';";
  html += "document.getElementById(`accYaw-${clientId}`).textContent = client.ACC_YAW + '°';";
  html += "document.getElementById(`zeroSet-${clientId}`).textContent = client.ZERO_SET;";
  html += "document.getElementById(`zeroSet-${clientId}`).style.color = client.ZERO_SET === 'true' ? '#28a745' : '#dc3545';";
  html += "document.getElementById(`zeroPitch-${clientId}`).textContent = client.ZERO_PITCH + '°';";
  html += "document.getElementById(`zeroRoll-${clientId}`).textContent = client.ZERO_ROLL + '°';";
  html += "document.getElementById(`zeroYaw-${clientId}`).textContent = client.ZERO_YAW + '°';";
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
  html += "console.log('WebSocket not connected');";
  html += "showNotification('WebSocket not connected', 'error');";
  html += "}";
  html += "}";
  html += "function sendDeviceCommand(deviceId, command) {";
  html += "if (ws && ws.readyState === WebSocket.OPEN) {";
  html += "ws.send('CMD:SEND_TO_DEVICE:' + deviceId + ':' + command);";
  html += "console.log('Sent command to ' + deviceId + ':', command);";
  html += "} else {";
  html += "console.log('WebSocket not connected');";
  html += "showNotification('WebSocket not connected', 'error');";
  html += "}";
  html += "}";
  html += "function showNotification(message, type = 'info') {";
  html += "const notification = document.createElement('div');";
  html += "notification.textContent = message;";
  html += "notification.style.cssText = `";
  html += "position: fixed;";
  html += "top: 20px;";
  html += "right: 20px;";
  html += "padding: 15px 20px;";
  html += "border-radius: 5px;";
  html += "color: white;";
  html += "font-weight: bold;";
  html += "z-index: 1000;";
  html += "background: ${type === 'success' ? '#28a745' : type === 'error' ? '#dc3545' : '#17a2b8'};";
  html += "box-shadow: 0 4px 12px rgba(0,0,0,0.3);";
  html += "`;";
  html += "document.body.appendChild(notification);";
  html += "setTimeout(() => {";
  html += "if (notification.parentNode) {";
  html += "notification.parentNode.removeChild(notification);";
  html += "}";
  html += "}, 3000);";
  html += "}";
  html += "window.addEventListener('load', function() {";
  html += "connectWebSocket();";
  html += "setInterval(() => {";
  html += "sendCommand('GET_ALL_DATA');";
  html += "}, 2000);";
  html += "});";
  html += "</script>";
  html += "</body></html>";

  addCORSHeaders();
  server.send(200, "text/html", html);
}

// API для получения данных клиентов
void handleAPIClients() {
  addCORSHeaders();
  
  DynamicJsonDocument doc(2048);
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
      client["zeroPitch"] = clients[i].zeroPitch;    // ДОБАВЛЕНО
      client["zeroRoll"] = clients[i].zeroRoll;      // ДОБАВЛЕНО
      client["zeroYaw"] = clients[i].zeroYaw;        // ДОБАВЛЕНО
      client["lastUpdate"] = clients[i].lastUpdate;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// API для отправки команд устройствам
void handleAPICommand() {
  addCORSHeaders();
  
  if (server.method() == HTTP_POST) {
    String deviceId = server.arg("deviceId");
    String command = server.arg("command");
    
    if (deviceId != "" && command != "") {
      Serial.println("API Command for " + deviceId + ": " + command);
      
      // Отправляем команду через WebSocket
      int clientIndex = findClientIndex(deviceId);
      if (clientIndex != -1 && clients[clientIndex].connected) {
        // ИСПРАВЛЕНИЕ: создаем переменную для передачи в broadcastTXT
        String broadcastMessage = "CMD:SEND_TO_DEVICE:" + deviceId + ":" + command;
        webSocket.broadcastTXT(broadcastMessage);
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Command sent\"}");
      } else {
        server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Device not found\"}");
      }
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  
  // Запуск точки доступа
  Serial.println("Starting Access Point...");
  WiFi.softAP(ap_ssid, ap_password);
  
  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Настройка Web сервера
  server.on("/", handleRoot);
  server.on("/api/clients", HTTP_GET, handleAPIClients);
  server.on("/api/command", HTTP_POST, handleAPICommand);
  
  // OPTIONS handlers для CORS
  server.on("/api/clients", HTTP_OPTIONS, handleOptions);
  server.on("/api/command", HTTP_OPTIONS, handleOptions);
  
  server.enableCORS(true);
  server.begin();
  
  // Запуск WebSocket сервера
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("HTTP server started on port 80");
  Serial.println("WebSocket server started on port 81");
  Serial.println("Server is ready! Connect to WiFi: " + String(ap_ssid));
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Очистка устаревших клиентов
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 1000) {
    cleanupOldClients();
    lastCleanup = millis();
  }
  
  // Автоматическая отправка данных каждые 100мс
  static unsigned long lastBroadcast = 0;
  if (millis() - lastBroadcast > 100) {
    broadcastData();
    lastBroadcast = millis();
  }
}
