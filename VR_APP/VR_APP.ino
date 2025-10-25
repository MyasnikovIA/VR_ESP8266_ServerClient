#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

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
  bool ipFixed;
};

// Структура для фиксированных IP адресов
struct FixedIP {
  String mac;
  String ip;
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 50;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Массив для фиксированных IP адресов
const int MAX_FIXED_IPS = 20;
FixedIP fixedIPs[MAX_FIXED_IPS];
int fixedIPCount = 0;

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// HTML страница - сильно упрощенная
const String htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Network Monitor</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }
        .header { text-align: center; margin-bottom: 20px; padding: 20px; background: #2c3e50; color: white; border-radius: 8px; }
        .stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin-bottom: 20px; }
        .stat-card { background: #ecf0f1; padding: 10px; border-radius: 5px; text-align: center; }
        .stat-number { font-size: 20px; font-weight: bold; color: #2c3e50; }
        .stat-label { color: #7f8c8d; font-size: 12px; }
        .devices-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 10px; margin-bottom: 20px; }
        .device-card { background: white; border: 1px solid #ddd; border-radius: 5px; padding: 10px; }
        .device-header { display: flex; justify-content: space-between; margin-bottom: 8px; }
        .device-name { font-weight: bold; color: #2c3e50; }
        .device-status { background: #27ae60; color: white; padding: 2px 6px; border-radius: 8px; font-size: 10px; }
        .status-fixed { background: #e67e22; }
        .info-row { display: flex; justify-content: space-between; margin-bottom: 3px; font-size: 12px; }
        .info-label { color: #7f8c8d; }
        .info-value { color: #2c3e50; font-family: monospace; }
        .controls { text-align: center; margin: 15px 0; }
        .btn { padding: 6px 12px; margin: 3px; border: none; border-radius: 3px; background: #3498db; color: white; cursor: pointer; font-size: 11px; }
        .btn:hover { background: #2980b9; }
        .btn-fixed { background: #9b59b6; }
        .refresh-loading { background: #f39c12 !important; }
        .last-update { text-align: center; color: #7f8c8d; font-size: 12px; margin-top: 15px; }
        .no-devices { text-align: center; color: #7f8c8d; padding: 20px; }
        .fixed-badge { background: #9b59b6; color: white; padding: 1px 4px; border-radius: 6px; font-size: 9px; margin-left: 3px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🖥️ Network Monitor</h1>
            <p>Real-time device monitoring</p>
        </div>
        
        <div class="stats">
            <div class="stat-card">
                <div class="stat-number" id="totalDevices">0</div>
                <div class="stat-label">Total Devices</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="onlineDevices">0</div>
                <div class="stat-label">Online Now</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="espIp">-</div>
                <div class="stat-label">ESP IP</div>
            </div>
        </div>
        
        <h3>📱 Connected Devices</h3>
        <div id="devicesContainer" class="devices-grid">
            <div class="no-devices">No devices found</div>
        </div>
        
        <div class="controls">
            <button class="btn" onclick="refreshData()" id="refreshBtn">🔄 Refresh</button>
            <button class="btn" onclick="startAutoRefresh()" id="autoRefreshBtn">🔄 Auto (10s)</button>
        </div>
        
        <div class="last-update">
            Last updated: <span id="updateTime">Never</span>
        </div>
    </div>

    <script>
        let autoRefreshInterval = null;
        
        function refreshData() {
            const btn = document.getElementById('refreshBtn');
            btn.innerHTML = '⏳ Scanning...';
            btn.disabled = true;
            
            fetch('/api/devices')
                .then(r => r.json())
                .then(data => {
                    updateDisplay(data);
                })
                .catch(e => {
                    console.error('Error:', e);
                })
                .finally(() => {
                    btn.innerHTML = '🔄 Refresh';
                    btn.disabled = false;
                });
        }
        
        function updateDisplay(data) {
            document.getElementById('totalDevices').textContent = data.totalDevices;
            document.getElementById('onlineDevices').textContent = data.onlineDevices;
            document.getElementById('espIp').textContent = data.espIp;
            document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();
            
            const container = document.getElementById('devicesContainer');
            
            if (!data.devices || data.devices.length === 0) {
                container.innerHTML = '<div class="no-devices">No devices found on network</div>';
                return;
            }
            
            container.innerHTML = data.devices.map(device => `
                <div class="device-card">
                    <div class="device-header">
                        <div class="device-name">
                            ${getDeviceIcon(device)} ${device.hostname || 'Unknown'}
                            ${device.ipFixed ? '<span class="fixed-badge">Fixed</span>' : ''}
                        </div>
                        <div class="device-status ${device.ipFixed ? 'status-fixed' : ''}">Online</div>
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
                        <span class="info-label">Signal:</span>
                        <span class="info-value">${device.rssi} dBm</span>
                    </div>
                    <div style="text-align: center; margin-top: 8px;">
                        <button class="btn btn-fixed" onclick="fixIP('${device.mac}', '${device.ip}')">
                            📌 Fix IP
                        </button>
                    </div>
                </div>
            `).join('');
        }
        
        function getDeviceIcon(device) {
            const mac = (device.mac || '').toLowerCase();
            const hostname = (device.hostname || '').toLowerCase();
            
            if (mac.includes('apple') || hostname.includes('iphone') || hostname.includes('ipad')) return '📱';
            if (hostname.includes('android')) return '📱';
            if (hostname.includes('pc') || hostname.includes('desktop') || hostname.includes('laptop')) return '💻';
            if (mac.startsWith('b8:27:eb') || hostname.includes('raspberry')) return '🍓';
            return '🔌';
        }
        
        function formatMac(mac) {
            return mac ? mac.toUpperCase().match(/.{1,2}/g).join(':') : '';
        }
        
        function fixIP(mac, ip) {
            if (confirm(`Fix IP ${ip} for MAC ${formatMac(mac)}?`)) {
                fetch('/api/fixip', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({mac: mac, ip: ip})
                })
                .then(r => r.json())
                .then(data => {
                    if (data.status === 'success') {
                        alert('IP fixed!');
                        refreshData();
                    } else {
                        alert('Error: ' + (data.message || 'Unknown error'));
                    }
                })
                .catch(e => {
                    console.error('Error:', e);
                    alert('Error fixing IP');
                });
            }
        }
        
        function startAutoRefresh() {
            const btn = document.getElementById('autoRefreshBtn');
            
            if (autoRefreshInterval) {
                clearInterval(autoRefreshInterval);
                autoRefreshInterval = null;
                btn.innerHTML = '🔄 Auto (10s)';
            } else {
                autoRefreshInterval = setInterval(refreshData, 10000);
                btn.innerHTML = '⏹️ Stop';
                refreshData();
            }
        }
        
        // Load data on start
        document.addEventListener('DOMContentLoaded', refreshData);
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

// Функция для поиска фиксированного IP по MAC
int findFixedIPByMAC(const String& mac) {
  for (int i = 0; i < fixedIPCount; i++) {
    if (fixedIPs[i].mac == mac) {
      return i;
    }
  }
  return -1;
}

// Функция для добавления нового устройства
void addDevice(const String& ip, const String& mac, const String& hostname, int rssi) {
  int index = findDeviceByMAC(mac);
  unsigned long currentTime = millis() / 1000;
  
  // Проверяем есть ли фиксированный IP для этого MAC
  int fixedIndex = findFixedIPByMAC(mac);
  String actualIP = ip;
  bool isFixed = false;
  
  if (fixedIndex != -1) {
    actualIP = fixedIPs[fixedIndex].ip;
    isFixed = true;
  }
  
  if (index == -1) {
    if (deviceCount < MAX_DEVICES) {
      devices[deviceCount].ip = actualIP;
      devices[deviceCount].mac = mac;
      devices[deviceCount].hostname = hostname;
      devices[deviceCount].firstSeen = currentTime;
      devices[deviceCount].lastSeen = currentTime;
      devices[deviceCount].rssi = rssi;
      devices[deviceCount].ipFixed = isFixed;
      deviceCount++;
      
      Serial.printf("New device: %s (%s) - %s - RSSI: %d - Fixed: %s\n", 
                   hostname.c_str(), actualIP.c_str(), mac.c_str(), rssi, isFixed ? "Yes" : "No");
    }
  } else {
    devices[index].lastSeen = currentTime;
    devices[index].rssi = rssi;
    devices[index].ip = actualIP;
    devices[index].ipFixed = isFixed;
    if (hostname.length() > 0 && devices[index].hostname != hostname) {
      devices[index].hostname = hostname;
    }
  }
}

// Функция для фиксации IP адреса
bool fixIPAddress(const String& mac, const String& ip) {
  int index = findFixedIPByMAC(mac);
  
  if (index == -1) {
    if (fixedIPCount < MAX_FIXED_IPS) {
      fixedIPs[fixedIPCount].mac = mac;
      fixedIPs[fixedIPCount].ip = ip;
      fixedIPCount++;
      
      // Обновляем устройство если оно есть в списке
      int deviceIndex = findDeviceByMAC(mac);
      if (deviceIndex != -1) {
        devices[deviceIndex].ip = ip;
        devices[deviceIndex].ipFixed = true;
      }
      
      saveFixedIPsToEEPROM();
      return true;
    }
  } else {
    fixedIPs[index].ip = ip;
    
    // Обновляем устройство если оно есть в списке
    int deviceIndex = findDeviceByMAC(mac);
    if (deviceIndex != -1) {
      devices[deviceIndex].ip = ip;
      devices[deviceIndex].ipFixed = true;
    }
    
    saveFixedIPsToEEPROM();
    return true;
  }
  return false;
}

// Функция для сканирования сети
void scanNetwork() {
  struct station_info *station = wifi_softap_get_station_info();
  int onlineCount = 0;
  
  while (station != NULL) {
    String ip = IPAddress(station->ip).toString();
    String mac = "";
    for(int i = 0; i < 6; i++) {
      if (i > 0) mac += ":";
      String hex = String(station->bssid[i], HEX);
      if (hex.length() == 1) hex = "0" + hex;
      mac += hex;
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

// Функция для сохранения фиксированных IP в EEPROM
void saveFixedIPsToEEPROM() {
  EEPROM.begin(512);
  
  // Используем простой формат для экономии памяти
  int addr = 0;
  EEPROM.write(addr++, fixedIPCount);
  
  for (int i = 0; i < fixedIPCount; i++) {
    // Сохраняем MAC
    String mac = fixedIPs[i].mac;
    EEPROM.write(addr++, mac.length());
    for (int j = 0; j < mac.length(); j++) {
      EEPROM.write(addr++, mac[j]);
    }
    
    // Сохраняем IP
    String ip = fixedIPs[i].ip;
    EEPROM.write(addr++, ip.length());
    for (int j = 0; j < ip.length(); j++) {
      EEPROM.write(addr++, ip[j]);
    }
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("Fixed IPs saved to EEPROM");
}

// Функция для загрузки фиксированных IP из EEPROM
void loadFixedIPsFromEEPROM() {
  EEPROM.begin(512);
  
  int addr = 0;
  fixedIPCount = EEPROM.read(addr++);
  
  if (fixedIPCount > MAX_FIXED_IPS) fixedIPCount = 0;
  
  for (int i = 0; i < fixedIPCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    String mac = "";
    for (int j = 0; j < macLen; j++) {
      mac += char(EEPROM.read(addr++));
    }
    
    // Загружаем IP
    int ipLen = EEPROM.read(addr++);
    String ip = "";
    for (int j = 0; j < ipLen; j++) {
      ip += char(EEPROM.read(addr++));
    }
    
    fixedIPs[i].mac = mac;
    fixedIPs[i].ip = ip;
  }
  
  EEPROM.end();
  
  Serial.printf("Fixed IPs loaded from EEPROM: %d\n", fixedIPCount);
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
  server.send(200, "text/html; charset=UTF-8", htmlPage);
}

// API для получения списка устройств
void handleApiDevices() {
  String json = "{";
  json += "\"totalDevices\":" + String(deviceCount) + ",";
  json += "\"onlineDevices\":" + String(deviceCount) + ",";
  json += "\"espIp\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"devices\":[";
  
  for (int i = 0; i < deviceCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ip\":\"" + devices[i].ip + "\",";
    json += "\"mac\":\"" + devices[i].mac + "\",";
    json += "\"hostname\":\"" + devices[i].hostname + "\",";
    json += "\"rssi\":" + String(devices[i].rssi) + ",";
    json += "\"ipFixed\":" + String(devices[i].ipFixed ? "true" : "false");
    json += "}";
  }
  
  json += "]}";
  
  server.send(200, "application/json; charset=UTF-8", json);
}

// API для фиксации IP адреса
void handleApiFixIP() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String ip = server.arg("ip");
    
    if (mac.length() > 0 && ip.length() > 0) {
      if (fixIPAddress(mac, ip)) {
        server.send(200, "application/json", "{\"status\":\"success\"}");
      } else {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Cannot fix IP\"}");
      }
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
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
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nStarting ESP8266 Network Monitor...");
  
  // Очистка EEPROM при первом запуске (раскомментировать для очистки)
  // clearEEPROM();
  
  // Загрузка фиксированных IP из EEPROM
  loadFixedIPsFromEEPROM();
  
  // Настройка WiFi
  setupWiFi();
  
  // Настройка маршрутов сервера
  server.on("/", handleRoot);
  server.on("/api/devices", handleApiDevices);
  server.on("/api/fixip", HTTP_POST, handleApiFixIP);
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP server started");
  
  // Первое сканирование
  scanNetwork();
  
  Serial.println("Setup completed successfully!");
}

void loop() {
  server.handleClient();
  
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    scanNetwork();
  }
  
  delay(100);
}
