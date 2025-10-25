#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>

// Настройки WiFi
const char* ssid = "ESP8266_Network_Monitor";
const char* password = "12345678";

// Создание веб-сервера на порту 80
ESP8266WebServer server(80);

// Структура для хранения информации об устройстве
struct DeviceInfo {
  String ip;
  String mac;
  String hostname;
  unsigned long firstSeen;
  unsigned long lastSeen;
  int rssi;
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 50;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// HTML страница - упрощенная версия
const String htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>ESP8266 Network Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: #f0f0f0;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
            padding: 20px;
            background: #2c3e50;
            color: white;
            border-radius: 8px;
        }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 30px;
        }
        .stat-card {
            background: #ecf0f1;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
            border-left: 4px solid #3498db;
        }
        .stat-number {
            font-size: 24px;
            font-weight: bold;
            color: #2c3e50;
        }
        .stat-label {
            color: #7f8c8d;
            font-size: 14px;
        }
        .devices-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
            gap: 15px;
            margin-bottom: 20px;
        }
        .device-card {
            background: white;
            border: 1px solid #ddd;
            border-radius: 8px;
            padding: 15px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .device-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
            border-bottom: 1px solid #eee;
            padding-bottom: 10px;
        }
        .device-name {
            font-weight: bold;
            color: #2c3e50;
        }
        .device-status {
            background: #27ae60;
            color: white;
            padding: 4px 8px;
            border-radius: 12px;
            font-size: 12px;
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            margin-bottom: 5px;
            font-size: 14px;
        }
        .info-label {
            color: #7f8c8d;
        }
        .info-value {
            color: #2c3e50;
            font-family: monospace;
        }
        .controls {
            text-align: center;
            margin: 20px 0;
        }
        .btn {
            padding: 10px 20px;
            margin: 5px;
            border: none;
            border-radius: 5px;
            background: #3498db;
            color: white;
            cursor: pointer;
            font-size: 14px;
        }
        .btn:hover {
            background: #2980b9;
        }
        .btn-success {
            background: #27ae60;
        }
        .btn-warning {
            background: #e67e22;
        }
        .refresh-loading {
            background: #f39c12 !important;
        }
        .last-update {
            text-align: center;
            color: #7f8c8d;
            font-size: 14px;
            margin-top: 20px;
        }
        .no-devices {
            text-align: center;
            color: #7f8c8d;
            padding: 40px;
            grid-column: 1 / -1;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🖥️ Network Monitor</h1>
            <p>Real-time device monitoring on ESP8266</p>
        </div>
        
        <div class="stats">
            <div class="stat-card">
                <div class="stat-number" id="totalDevices">0</div>
                <div class="stat-label">Total Devices</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="onlineDevices">0</div>
                <div class="stat-label">Currently Online</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="espIp">-</div>
                <div class="stat-label">ESP IP Address</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="lastScan">-</div>
                <div class="stat-label">Last Scan</div>
            </div>
        </div>
        
        <h2>📱 Connected Devices</h2>
        <div id="devicesContainer" class="devices-grid">
            <div class="no-devices">No devices found on the network</div>
        </div>
        
        <div class="controls">
            <button class="btn" onclick="refreshData()" id="refreshBtn">🔄 Refresh Now</button>
            <button class="btn btn-success" onclick="startAutoRefresh()" id="autoRefreshBtn">🔄 Auto Refresh (10s)</button>
            <button class="btn btn-warning" onclick="clearData()">🗑️ Clear History</button>
        </div>
        
        <div class="last-update">
            Last updated: <span id="updateTime">Never</span>
        </div>
    </div>

    <script>
        let autoRefreshInterval = null;
        
        function refreshData() {
            const refreshBtn = document.getElementById('refreshBtn');
            const originalText = refreshBtn.innerHTML;
            refreshBtn.innerHTML = '⏳ Scanning...';
            refreshBtn.classList.add('refresh-loading');
            refreshBtn.disabled = true;
            
            fetch('/api/devices')
                .then(response => response.json())
                .then(data => {
                    updateDisplay(data);
                })
                .catch(error => {
                    console.error('Error:', error);
                })
                .finally(() => {
                    refreshBtn.innerHTML = originalText;
                    refreshBtn.classList.remove('refresh-loading');
                    refreshBtn.disabled = false;
                });
        }
        
        function updateDisplay(data) {
            document.getElementById('totalDevices').textContent = data.totalDevices;
            document.getElementById('onlineDevices').textContent = data.onlineDevices;
            document.getElementById('espIp').textContent = data.espIp;
            document.getElementById('lastScan').textContent = data.lastScanTime + 's ago';
            document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();
            
            const container = document.getElementById('devicesContainer');
            
            if (data.devices.length === 0) {
                container.innerHTML = '<div class="no-devices">No devices found on the network</div>';
                return;
            }
            
            container.innerHTML = data.devices.map(device => `
                <div class="device-card">
                    <div class="device-header">
                        <div class="device-name">${getDeviceIcon(device)} ${device.hostname || 'Unknown Device'}</div>
                        <div class="device-status">Online</div>
                    </div>
                    <div class="info-row">
                        <span class="info-label">IP:</span>
                        <span class="info-value">${device.ip}</span>
                    </div>
                    <div class="info-row">
                        <span class="info-label">MAC:</span>
                        <span class="info-value">${formatMac(device.mac)}</span>
                    </div>
                    <div class="info-row">
                        <span class="info-label">First Seen:</span>
                        <span class="info-value">${formatTime(device.firstSeen)}</span>
                    </div>
                    <div class="info-row">
                        <span class="info-label">Last Seen:</span>
                        <span class="info-value">${formatTime(device.lastSeen)}</span>
                    </div>
                    <div class="info-row">
                        <span class="info-label">Signal:</span>
                        <span class="info-value">${device.rssi} dBm</span>
                    </div>
                </div>
            `).join('');
        }
        
        function getDeviceIcon(device) {
            const mac = device.mac.toLowerCase();
            const hostname = (device.hostname || '').toLowerCase();
            
            if (mac.includes('apple') || hostname.includes('iphone') || hostname.includes('ipad')) {
                return '📱';
            } else if (hostname.includes('android')) {
                return '📱';
            } else if (hostname.includes('pc') || hostname.includes('desktop') || hostname.includes('laptop')) {
                return '💻';
            } else if (mac.startsWith('b8:27:eb') || hostname.includes('raspberry')) {
                return '🍓';
            }
            return '🔌';
        }
        
        function formatMac(mac) {
            return mac.toUpperCase().match(/.{1,2}/g).join(':');
        }
        
        function formatTime(timestamp) {
            const now = Math.floor(Date.now() / 1000);
            const diff = now - timestamp;
            
            if (diff < 60) return 'Just now';
            if (diff < 3600) return Math.floor(diff / 60) + ' min ago';
            if (diff < 86400) return Math.floor(diff / 3600) + ' hours ago';
            return Math.floor(diff / 86400) + ' days ago';
        }
        
        function startAutoRefresh() {
            const btn = document.getElementById('autoRefreshBtn');
            
            if (autoRefreshInterval) {
                clearInterval(autoRefreshInterval);
                autoRefreshInterval = null;
                btn.innerHTML = '🔄 Auto Refresh (10s)';
                btn.className = 'btn btn-success';
            } else {
                autoRefreshInterval = setInterval(refreshData, 10000);
                btn.innerHTML = '⏹️ Stop Auto Refresh';
                btn.className = 'btn';
                refreshData();
            }
        }
        
        function clearData() {
            if (confirm('Are you sure you want to clear all device history?')) {
                fetch('/api/clear', { method: 'POST' })
                    .then(() => refreshData())
                    .catch(error => console.error('Error:', error));
            }
        }
        
        // Загружаем данные при старте
        document.addEventListener('DOMContentLoaded', function() {
            refreshData();
        });
    </script>
</body>
</html>
)rawliteral";

// Функция для поиска устройства в массиве
int findDeviceByMAC(const String& mac) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].mac == mac) {
      return i;
    }
  }
  return -1;
}

// Функция для добавления нового устройства
void addDevice(const String& ip, const String& mac, const String& hostname, int rssi) {
  int index = findDeviceByMAC(mac);
  unsigned long currentTime = millis() / 1000;
  
  if (index == -1) {
    if (deviceCount < MAX_DEVICES) {
      devices[deviceCount].ip = ip;
      devices[deviceCount].mac = mac;
      devices[deviceCount].hostname = hostname;
      devices[deviceCount].firstSeen = currentTime;
      devices[deviceCount].lastSeen = currentTime;
      devices[deviceCount].rssi = rssi;
      deviceCount++;
      
      Serial.printf("New device: %s (%s) - %s - RSSI: %d\n", 
                   hostname.c_str(), ip.c_str(), mac.c_str(), rssi);
    }
  } else {
    devices[index].lastSeen = currentTime;
    devices[index].rssi = rssi;
    if (hostname.length() > 0 && devices[index].hostname != hostname) {
      devices[index].hostname = hostname;
    }
  }
}

// Функция для сканирования сети
void scanNetwork() {
  Serial.println("Scanning network...");
  
  struct station_info *station = wifi_softap_get_station_info();
  int onlineCount = 0;
  
  while (station != NULL) {
    String ip = IPAddress(station->ip).toString();
    String mac = "";
    for(int i = 0; i < 6; i++) {
      if (i > 0) mac += ":";
      mac += String(station->bssid[i], HEX);
    }
    
    String hostname = "";
    int rssi = -50;
    
    addDevice(ip, mac, hostname, rssi);
    onlineCount++;
    
    station = STAILQ_NEXT(station, next);
  }
  
  wifi_softap_free_station_info();
  
  Serial.printf("Scan complete. Found %d online devices\n", onlineCount);
  lastScanTime = millis();
}

// Функция для очистки EEPROM
void clearEEPROM() {
  EEPROM.begin(512);
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("EEPROM cleared successfully");
}

// Обработчик главной страницы
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// API для получения списка устройств
void handleApiDevices() {
  String json = "{";
  json += "\"totalDevices\":" + String(deviceCount) + ",";
  json += "\"onlineDevices\":" + String(deviceCount) + ",";
  json += "\"espIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"lastScanTime\":" + String((millis() - lastScanTime) / 1000) + ",";
  json += "\"devices\":[";
  
  for (int i = 0; i < deviceCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ip\":\"" + devices[i].ip + "\",";
    json += "\"mac\":\"" + devices[i].mac + "\",";
    json += "\"hostname\":\"" + devices[i].hostname + "\",";
    json += "\"firstSeen\":" + String(devices[i].firstSeen) + ",";
    json += "\"lastSeen\":" + String(devices[i].lastSeen) + ",";
    json += "\"rssi\":" + String(devices[i].rssi);
    json += "}";
  }
  
  json += "]}";
  
  server.send(200, "application/json", json);
}

// API для очистки данных
void handleApiClear() {
  deviceCount = 0;
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

// Настройка WiFi в режиме точки доступа
void setupWiFi() {
  Serial.println("Setting up Access Point...");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  
  Serial.println("Access Point Started");
  Serial.print("SSID: "); Serial.println(ssid);
  Serial.print("IP address: "); Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nStarting ESP8266 Network Monitor...");
  
  // Очистка EEPROM при первом запуске
  clearEEPROM();
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка mDNS
  if (!MDNS.begin("esp8266")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started");
  }
  
  // Настройка маршрутов сервера
  server.on("/", handleRoot);
  server.on("/api/devices", handleApiDevices);
  server.on("/api/clear", HTTP_POST, handleApiClear);
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP server started");
  
  // Первое сканирование
  scanNetwork();
  
  Serial.println("Setup completed successfully!");
}

void loop() {
  server.handleClient();
  MDNS.update();
  
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    scanNetwork();
  }
  
  delay(100);
}
