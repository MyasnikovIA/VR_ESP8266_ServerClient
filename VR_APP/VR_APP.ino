#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

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
  int rssi; // Сила сигнала
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 50;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000; // Сканировать каждые 10 секунд

// HTML страница
const String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP8266 Network Monitor</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 15px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            overflow: hidden;
        }
        
        .header {
            background: linear-gradient(135deg, #2c3e50, #34495e);
            color: white;
            padding: 30px;
            text-align: center;
        }
        
        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            font-weight: 300;
        }
        
        .header p {
            font-size: 1.1em;
            opacity: 0.9;
        }
        
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            padding: 20px;
            background: #f8f9fa;
        }
        
        .stat-card {
            background: white;
            padding: 20px;
            border-radius: 10px;
            text-align: center;
            box-shadow: 0 5px 15px rgba(0, 0, 0, 0.1);
            border-left: 4px solid #3498db;
        }
        
        .stat-number {
            font-size: 2.5em;
            font-weight: bold;
            color: #2c3e50;
            margin-bottom: 5px;
        }
        
        .stat-label {
            color: #7f8c8d;
            font-size: 0.9em;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .devices-section {
            padding: 30px;
        }
        
        .section-title {
            font-size: 1.8em;
            color: #2c3e50;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 2px solid #ecf0f1;
        }
        
        .devices-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(350px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .device-card {
            background: white;
            border-radius: 12px;
            padding: 20px;
            box-shadow: 0 8px 25px rgba(0, 0, 0, 0.1);
            border: 1px solid #e1e8ed;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
        }
        
        .device-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 15px 35px rgba(0, 0, 0, 0.15);
        }
        
        .device-card::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            width: 5px;
            height: 100%;
            background: #3498db;
        }
        
        .device-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
        }
        
        .device-name {
            font-size: 1.3em;
            font-weight: 600;
            color: #2c3e50;
        }
        
        .device-status {
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: bold;
            text-transform: uppercase;
        }
        
        .status-online {
            background: #d4edda;
            color: #155724;
        }
        
        .device-info {
            margin-bottom: 10px;
        }
        
        .info-row {
            display: flex;
            justify-content: space-between;
            margin-bottom: 8px;
            padding: 5px 0;
            border-bottom: 1px solid #f8f9fa;
        }
        
        .info-label {
            font-weight: 600;
            color: #7f8c8d;
            font-size: 0.9em;
        }
        
        .info-value {
            color: #2c3e50;
            font-family: 'Courier New', monospace;
            font-size: 0.9em;
        }
        
        .signal-strength {
            margin-top: 15px;
            padding-top: 15px;
            border-top: 1px solid #ecf0f1;
        }
        
        .signal-bar {
            height: 8px;
            background: #ecf0f1;
            border-radius: 4px;
            overflow: hidden;
            margin-top: 5px;
        }
        
        .signal-level {
            height: 100%;
            border-radius: 4px;
            transition: width 0.3s ease;
        }
        
        .signal-excellent { background: #27ae60; }
        .signal-good { background: #2ecc71; }
        .signal-fair { background: #f39c12; }
        .signal-weak { background: #e74c3c; }
        
        .controls {
            text-align: center;
            padding: 20px;
            background: #f8f9fa;
            border-top: 1px solid #e1e8ed;
        }
        
        .btn {
            padding: 12px 30px;
            border: none;
            border-radius: 25px;
            font-size: 1em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            margin: 0 10px;
        }
        
        .btn-primary {
            background: linear-gradient(135deg, #3498db, #2980b9);
            color: white;
        }
        
        .btn-success {
            background: linear-gradient(135deg, #27ae60, #229954);
            color: white;
        }
        
        .btn-warning {
            background: linear-gradient(135deg, #f39c12, #e67e22);
            color: white;
        }
        
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 20px rgba(0, 0, 0, 0.2);
        }
        
        .last-update {
            text-align: center;
            padding: 15px;
            color: #7f8c8d;
            font-size: 0.9em;
            background: #f8f9fa;
            border-top: 1px solid #e1e8ed;
        }
        
        .no-devices {
            text-align: center;
            padding: 40px;
            color: #7f8c8d;
            font-size: 1.1em;
        }
        
        .device-icon {
            font-size: 1.5em;
            margin-right: 10px;
        }
        
        @media (max-width: 768px) {
            .devices-grid {
                grid-template-columns: 1fr;
            }
            
            .header h1 {
                font-size: 2em;
            }
            
            .stats {
                grid-template-columns: 1fr;
            }
        }
        
        .refresh-animation {
            animation: refreshSpin 1s ease;
        }
        
        @keyframes refreshSpin {
            from { transform: rotate(0deg); }
            to { transform: rotate(360deg); }
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
        
        <div class="devices-section">
            <h2 class="section-title">📱 Connected Devices</h2>
            <div id="devicesContainer" class="devices-grid">
                <div class="no-devices">No devices found on the network</div>
            </div>
        </div>
        
        <div class="controls">
            <button class="btn btn-primary" onclick="refreshData()" id="refreshBtn">
                🔄 Refresh Now
            </button>
            <button class="btn btn-success" onclick="startAutoRefresh()" id="autoRefreshBtn">
                🔄 Auto Refresh (10s)
            </button>
            <button class="btn btn-warning" onclick="clearData()">
                🗑️ Clear History
            </button>
        </div>
        
        <div class="last-update">
            Last updated: <span id="updateTime">Never</span>
        </div>
    </div>

    <script>
        let autoRefreshInterval = null;
        
        // Функция для обновления данных
        async function refreshData() {
            const refreshBtn = document.getElementById('refreshBtn');
            refreshBtn.innerHTML = '⏳ Scanning...';
            refreshBtn.disabled = true;
            
            try {
                const response = await fetch('/api/devices');
                const data = await response.json();
                updateDisplay(data);
                
                // Анимация обновления
                refreshBtn.classList.add('refresh-animation');
                setTimeout(() => {
                    refreshBtn.classList.remove('refresh-animation');
                }, 1000);
                
            } catch (error) {
                console.error('Error fetching data:', error);
            } finally {
                refreshBtn.innerHTML = '🔄 Refresh Now';
                refreshBtn.disabled = false;
            }
        }
        
        // Функция для обновления отображения
        function updateDisplay(data) {
            // Обновляем статистику
            document.getElementById('totalDevices').textContent = data.totalDevices;
            document.getElementById('onlineDevices').textContent = data.onlineDevices;
            document.getElementById('espIp').textContent = data.espIp;
            document.getElementById('lastScan').textContent = data.lastScanTime + 's ago';
            document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();
            
            // Обновляем список устройств
            const container = document.getElementById('devicesContainer');
            
            if (data.devices.length === 0) {
                container.innerHTML = '<div class="no-devices">No devices found on the network</div>';
                return;
            }
            
            container.innerHTML = data.devices.map(device => `
                <div class="device-card">
                    <div class="device-header">
                        <div class="device-name">
                            ${getDeviceIcon(device)} ${device.hostname || 'Unknown Device'}
                        </div>
                        <div class="device-status status-online">Online</div>
                    </div>
                    
                    <div class="device-info">
                        <div class="info-row">
                            <span class="info-label">IP Address:</span>
                            <span class="info-value">${device.ip}</span>
                        </div>
                        <div class="info-row">
                            <span class="info-label">MAC Address:</span>
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
                    </div>
                    
                    <div class="signal-strength">
                        <div class="info-row">
                            <span class="info-label">Signal Strength:</span>
                            <span class="info-value">${device.rssi} dBm</span>
                        </div>
                        <div class="signal-bar">
                            <div class="signal-level ${getSignalClass(device.rssi)}" 
                                 style="width: ${calculateSignalWidth(device.rssi)}%"></div>
                        </div>
                    </div>
                </div>
            `).join('');
        }
        
        // Функция для получения иконки устройства
        function getDeviceIcon(device) {
            const mac = device.mac.toLowerCase();
            if (mac.includes('apple') || device.hostname?.toLowerCase().includes('iphone') || 
                device.hostname?.toLowerCase().includes('ipad')) {
                return '📱';
            } else if (device.hostname?.toLowerCase().includes('android')) {
                return '📱';
            } else if (device.hostname?.toLowerCase().includes('pc') || 
                      device.hostname?.toLowerCase().includes('desktop') ||
                      device.hostname?.toLowerCase().includes('laptop')) {
                return '💻';
            } else if (mac.startsWith('b8:27:eb') || device.hostname?.toLowerCase().includes('raspberry')) {
                return '🍓';
            }
            return '🔌';
        }
        
        // Функция для форматирования MAC-адреса
        function formatMac(mac) {
            return mac.toUpperCase().match(/.{1,2}/g).join(':');
        }
        
        // Функция для форматирования времени
        function formatTime(timestamp) {
            const now = Math.floor(Date.now() / 1000);
            const diff = now - timestamp;
            
            if (diff < 60) return 'Just now';
            if (diff < 3600) return Math.floor(diff / 60) + ' min ago';
            if (diff < 86400) return Math.floor(diff / 3600) + ' hours ago';
            return Math.floor(diff / 86400) + ' days ago';
        }
        
        // Функция для расчета ширины сигнала
        function calculateSignalWidth(rssi) {
            // RSSI от -30 (отлично) до -90 (плохо)
            const minRssi = -90;
            const maxRssi = -30;
            let quality = Math.min(Math.max((rssi - minRssi) / (maxRssi - minRssi) * 100, 0), 100);
            return Math.round(quality);
        }
        
        // Функция для получения класса сигнала
        function getSignalClass(rssi) {
            if (rssi >= -50) return 'signal-excellent';
            if (rssi >= -60) return 'signal-good';
            if (rssi >= -70) return 'signal-fair';
            return 'signal-weak';
        }
        
        // Функция для автоматического обновления
        function startAutoRefresh() {
            const btn = document.getElementById('autoRefreshBtn');
            
            if (autoRefreshInterval) {
                clearInterval(autoRefreshInterval);
                autoRefreshInterval = null;
                btn.innerHTML = '🔄 Auto Refresh (10s)';
                btn.style.background = 'linear-gradient(135deg, #27ae60, #229954)';
            } else {
                autoRefreshInterval = setInterval(refreshData, 10000);
                btn.innerHTML = '⏹️ Stop Auto Refresh';
                btn.style.background = 'linear-gradient(135deg, #e74c3c, #c0392b)';
                refreshData(); // Сразу обновляем данные
            }
        }
        
        // Функция для очистки данных
        async function clearData() {
            if (confirm('Are you sure you want to clear all device history?')) {
                try {
                    await fetch('/api/clear', { method: 'POST' });
                    refreshData();
                } catch (error) {
                    console.error('Error clearing data:', error);
                }
            }
        }
        
        // Загружаем данные при старте
        document.addEventListener('DOMContentLoaded', function() {
            refreshData();
            // Запускаем автоматическое обновление каждые 30 секунд
            setInterval(refreshData, 30000);
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
    // Новое устройство
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
    // Обновляем существующее устройство
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
  
  // Получаем список станций в сети
  struct station_info *station = wifi_softap_get_station_info();
  int onlineCount = 0;
  
  while (station != NULL) {
    String ip = IPAddress(station->ip).toString();
    String mac = "";
    for(int i = 0; i < 6; i++) {
      if (i > 0) mac += ":";
      mac += String(station->bssid[i], HEX);
    }
    
    // Получаем hostname через mDNS (если доступно)
    String hostname = "";
    
    // Вместо station->rssi используем фиктивное значение или WiFi.RSSI()
    // Для подключенных станций RSSI недоступен напрямую через station_info
    int rssi = -50; // Фиктивное значение, так как RSSI недоступен
    
    addDevice(ip, mac, hostname, rssi);
    onlineCount++;
    
    station = STAILQ_NEXT(station, next);
  }
  
  wifi_softap_free_station_info();
  
  Serial.printf("Scan complete. Found %d online devices\n", onlineCount);
  lastScanTime = millis();
}

// Обработчик главной страницы
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// API для получения списка устройств
void handleApiDevices() {
  String json = "{";
  json += "\"totalDevices\":" + String(deviceCount) + ",";
  json += "\"onlineDevices\":" + String(deviceCount) + ","; // Все устройства считаем онлайн
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
  
  // Создаем точку доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  // Настраиваем AP
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  
  Serial.println("Access Point Started");
  Serial.print("SSID: "); Serial.println(ssid);
  Serial.print("IP address: "); Serial.println(WiFi.softAPIP());
  Serial.print("MAC address: "); Serial.println(WiFi.softAPmacAddress());
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting ESP8266 Network Monitor...");
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка mDNS
  if (!MDNS.begin("esp8266")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started");
    MDNS.addService("http", "tcp", 80);
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
}

void loop() {
  server.handleClient();
  MDNS.update();
  
  // Сканируем сеть каждые SCAN_INTERVAL миллисекунд
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    scanNetwork();
  }
  
  delay(100);
}
