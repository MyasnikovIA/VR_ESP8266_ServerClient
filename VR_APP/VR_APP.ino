#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// Настройки WiFi по умолчанию
const char* ap_ssid = "ESP8266_Network_Monitor";
const char* ap_password = "12345678";

// Создание веб-сервера на порту 80
ESP8266WebServer server(80);

// WebSocket сервер на порту 81 для VR-клиентов
WebSocketsServer webSocket = WebSocketsServer(81);

// WebSocket сервер на порту 82 для веб-клиентов (обновление данных в реальном времени)
WebSocketsServer webSocketWeb = WebSocketsServer(82);

// Структура для хранения информации об устройстве
struct DeviceInfo {
  String ip;
  String mac;
  String hostname;
  int rssi;
  bool ipFixed;
  bool hasMPU6050;
  float pitch;
  float roll;
  float yaw;
  float relPitch;
  float relRoll;
  float relYaw;
  unsigned long lastUpdate;
  bool connected;
};

// Структура для настроек сети
struct NetworkSettings {
  String ssid;
  String password;
  String subnet;
  bool configured;
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 20;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Настройки сети
NetworkSettings networkSettings;

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// Упрощенная HTML страница (старый функционал)
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>VR Tracking Server</title>
    <style>
        body { font-family: Arial; margin: 10px; background: #f0f0f0; }
        .container { max-width: 1000px; margin: 0 auto; background: white; padding: 15px; border-radius: 8px; }
        .header { text-align: center; margin-bottom: 15px; padding: 15px; background: #2c3e50; color: white; border-radius: 6px; }
        .stats { display: flex; justify-content: space-around; margin-bottom: 15px; flex-wrap: wrap; }
        .stat-card { background: #ecf0f1; padding: 8px; border-radius: 4px; text-align: center; min-width: 100px; margin: 3px; }
        .stat-number { font-size: 18px; font-weight: bold; color: #2c3e50; }
        .stat-label { color: #7f8c8d; font-size: 11px; }
        .devices-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 8px; margin-bottom: 15px; }
        .device-card { background: white; border: 1px solid #ddd; border-radius: 4px; padding: 8px; transition: all 0.3s; }
        .device-card.vr-device { border-left: 4px solid #e74c3c; background: #fff5f5; }
        .device-header { display: flex; justify-content: space-between; margin-bottom: 6px; }
        .device-name { font-weight: bold; color: #2c3e50; font-size: 14px; }
        .device-status { padding: 2px 5px; border-radius: 6px; font-size: 9px; }
        .status-online { background: #27ae60; color: white; }
        .status-offline { background: #95a5a6; color: white; }
        .info-row { display: flex; justify-content: space-between; margin-bottom: 2px; font-size: 11px; }
        .info-label { color: #7f8c8d; }
        .info-value { color: #2c3e50; font-family: monospace; }
        .sensor-data { background: #f8f9fa; padding: 5px; border-radius: 3px; margin-top: 5px; border: 1px solid #e9ecef; }
        .sensor-row { display: flex; justify-content: space-between; margin-bottom: 1px; font-size: 10px; }
        .sensor-label { color: #6c757d; }
        .sensor-value { color: #e74c3c; font-weight: bold; }
        .sensor-controls { display: flex; gap: 5px; margin-top: 5px; }
        .sensor-btn { padding: 3px 8px; border: none; border-radius: 3px; background: #3498db; color: white; cursor: pointer; font-size: 9px; flex: 1; }
        .sensor-btn.calibrate { background: #f39c12; }
        .sensor-btn.reset { background: #e74c3c; }
        .controls { text-align: center; margin: 10px 0; }
        .btn { padding: 5px 10px; margin: 2px; border: none; border-radius: 3px; background: #3498db; color: white; cursor: pointer; font-size: 10px; }
        .btn-settings { background: #34495e; }
        .last-update { text-align: center; color: #7f8c8d; font-size: 11px; margin-top: 10px; }
        .no-devices { text-align: center; color: #7f8c8d; padding: 15px; }
        .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }
        .modal-content { background-color: white; margin: 15% auto; padding: 15px; border-radius: 6px; width: 300px; max-width: 90%; }
        .modal-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .modal-title { font-weight: bold; font-size: 14px; }
        .close { color: #aaa; font-size: 18px; font-weight: bold; cursor: pointer; }
        .form-group { margin-bottom: 10px; }
        .form-label { display: block; margin-bottom: 3px; font-size: 11px; color: #7f8c8d; }
        .form-input { width: 100%; padding: 6px; border: 1px solid #ddd; border-radius: 3px; box-sizing: border-box; font-size: 11px; }
        .modal-buttons { display: flex; justify-content: flex-end; gap: 8px; }
        .warning { background: #fff3cd; border: 1px solid #ffeaa7; padding: 8px; border-radius: 3px; margin-bottom: 10px; font-size: 11px; color: #856404; }
        .vr-badge { background: #e74c3c; color: white; padding: 1px 4px; border-radius: 3px; font-size: 8px; margin-left: 5px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1 style="margin:0;font-size:20px;">🎮 VR Tracking Server</h1>
            <p style="margin:5px 0 0 0;font-size:12px;">Real-time MPU6050 device monitoring</p>
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
                <div class="stat-number" id="vrDevices">0</div>
                <div class="stat-label">VR Headsets</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="espIp">-</div>
                <div class="stat-label">Server IP</div>
            </div>
        </div>
        
        <h3 style="margin:0 0 10px 0;font-size:16px;">📱 Connected Devices</h3>
        <div id="devicesContainer" class="devices-grid">
            <div class="no-devices">No devices found</div>
        </div>
        
        <div class="controls">
            <button class="btn" onclick="refreshData()" id="refreshBtn">🔄 Refresh</button>
            <button class="btn" onclick="startAutoRefresh()" id="autoRefreshBtn">Auto (5s)</button>
            <button class="btn btn-settings" onclick="showSettings()">⚙️ Settings</button>
        </div>
        
        <div class="last-update">
            Last updated: <span id="updateTime">Never</span>
        </div>
    </div>

    <!-- Modal for network settings -->
    <div id="settingsModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <div class="modal-title">⚙️ Network Settings</div>
                <span class="close" onclick="closeModal('settingsModal')">&times;</span>
            </div>
            <div class="warning">
                ⚠️ Changing subnet will restart access point!
            </div>
            <div class="form-group">
                <label class="form-label">WiFi SSID:</label>
                <input type="text" id="settingsSsid" class="form-input" value="ESP8266_Network_Monitor" maxlength="20">
            </div>
            <div class="form-group">
                <label class="form-label">WiFi Password:</label>
                <input type="text" id="settingsPassword" class="form-input" value="12345678" maxlength="20">
            </div>
            <div class="form-group">
                <label class="form-label">Subnet (1-254):</label>
                <input type="number" id="settingsSubnet" class="form-input" value="4" min="1" max="254">
            </div>
            <div class="modal-buttons">
                <button class="btn" onclick="closeModal('settingsModal')">Cancel</button>
                <button class="btn btn-settings" onclick="saveSettings()">💾 Save & Restart</button>
            </div>
        </div>
    </div>

    <script>
        let autoRefreshInterval = null;
        let webSocketClient = null;
        
        function connectWebSocket() {
            const serverIp = document.getElementById('espIp').textContent;
            if (serverIp === '-') return;
            
            webSocketClient = new WebSocket(`ws://${serverIp}:82`);
            
            webSocketClient.onopen = function() {
                console.log('WebSocket connected for real-time updates');
            };
            
            webSocketClient.onmessage = function(event) {
                const data = JSON.parse(event.data);
                if (data.type === 'sensor_update') {
                    updateSensorData(data.device);
                }
            };
            
            webSocketClient.onclose = function() {
                console.log('WebSocket disconnected, reconnecting...');
                setTimeout(connectWebSocket, 3000);
            };
            
            webSocketClient.onerror = function(error) {
                console.error('WebSocket error:', error);
            };
        }
        
        function updateSensorData(deviceData) {
            const deviceCard = document.querySelector(`[data-ip="${deviceData.ip}"]`);
            if (deviceCard && deviceData.hasMPU6050) {
                // Находим все элементы сенсоров
                const sensorValues = deviceCard.querySelectorAll('.sensor-value');
                if (sensorValues.length >= 6) {
                    sensorValues[0].textContent = deviceData.pitch.toFixed(1) + '°';
                    sensorValues[1].textContent = deviceData.roll.toFixed(1) + '°';
                    sensorValues[2].textContent = deviceData.yaw.toFixed(1) + '°';
                    sensorValues[3].textContent = deviceData.relPitch.toFixed(1) + '°';
                    sensorValues[4].textContent = deviceData.relRoll.toFixed(1) + '°';
                    sensorValues[5].textContent = deviceData.relYaw.toFixed(1) + '°';
                }
                
                // Обновляем время последнего обновления
                const updateTime = deviceCard.querySelector('.last-sensor-update');
                if (updateTime) {
                    updateTime.textContent = new Date().toLocaleTimeString();
                }
            }
        }
        
        function calibrateSensor(ip) {
            
            fetch('/api/calibrate', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'ip=' + encodeURIComponent(ip)
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    console.log('Команда калибровки отправлена устройству');
                } else {
                    alert('Ошибка: ' + (data.message || 'Неизвестная ошибка'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Ошибка отправки команды калибровки');
            });
        }
        
        function resetAngles(ip) {
            fetch('/api/reset', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'ip=' + encodeURIComponent(ip)
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    console.log('Команда сброса углов отправлена устройству');
                } else {
                    alert('Ошибка: ' + (data.message || 'Неизвестная ошибка'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Ошибка отправки команды сброса');
            });
        }
        
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
                    alert('Error loading data');
                })
                .finally(() => {
                    btn.innerHTML = '🔄 Refresh';
                    btn.disabled = false;
                });
        }
        
        function updateDisplay(data) {
            document.getElementById('totalDevices').textContent = data.totalDevices;
            document.getElementById('onlineDevices').textContent = data.onlineDevices;
            document.getElementById('vrDevices').textContent = data.vrDevices;
            document.getElementById('espIp').textContent = data.espIp;
            document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();
            
            const container = document.getElementById('devicesContainer');
            
            if (!data.devices || data.devices.length === 0) {
                container.innerHTML = '<div class="no-devices">No devices found on network</div>';
                return;
            }
            
            container.innerHTML = data.devices.map(device => `
                <div class="device-card ${device.hasMPU6050 ? 'vr-device' : ''}" data-ip="${device.ip}">
                    <div class="device-header">
                        <div class="device-name">
                            ${getDeviceIcon(device)} ${device.displayName}
                            ${device.hasMPU6050 ? '<span class="vr-badge">VR</span>' : ''}
                        </div>
                        <div class="device-status ${device.connected ? 'status-online' : 'status-offline'}">
                            ${device.connected ? 'Online' : 'Offline'}
                        </div>
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
                    ${device.hasMPU6050 ? `
                    <div class="sensor-data">
                        <div class="sensor-row">
                            <span class="sensor-label">Pitch:</span>
                            <span class="sensor-value">${device.pitch.toFixed(1)}°</span>
                        </div>
                        <div class="sensor-row">
                            <span class="sensor-label">Roll:</span>
                            <span class="sensor-value">${device.roll.toFixed(1)}°</span>
                        </div>
                        <div class="sensor-row">
                            <span class="sensor-label">Yaw:</span>
                            <span class="sensor-value">${device.yaw.toFixed(1)}°</span>
                        </div>
                        <div class="sensor-row">
                            <span class="sensor-label">Rel Pitch:</span>
                            <span class="sensor-value">${device.relPitch.toFixed(1)}°</span>
                        </div>
                        <div class="sensor-row">
                            <span class="sensor-label">Rel Roll:</span>
                            <span class="sensor-value">${device.relRoll.toFixed(1)}°</span>
                        </div>
                        <div class="sensor-row">
                            <span class="sensor-label">Rel Yaw:</span>
                            <span class="sensor-value">${device.relYaw.toFixed(1)}°</span>
                        </div>
                        <div class="sensor-controls">
                            <button class="sensor-btn calibrate" onclick="calibrateSensor('${device.ip}')">Калибровка</button>
                            <button class="sensor-btn reset" onclick="resetAngles('${device.ip}')">Reset Angles</button>
                        </div>
                        <div class="sensor-row" style="font-size: 8px; color: #999; margin-top: 3px;">
                            <span class="sensor-label">Last sensor update:</span>
                            <span class="last-sensor-update">${new Date().toLocaleTimeString()}</span>
                        </div>
                    </div>
                    ` : ''}
                </div>
            `).join('');
        }
        
        function getDeviceIcon(device) {
            if (device.hasMPU6050) return '🎮';
            const name = (device.displayName || '').toLowerCase();
            if (name.includes('iphone') || name.includes('ipad')) return '📱';
            if (name.includes('android')) return '📱';
            if (name.includes('pc') || name.includes('desktop') || name.includes('laptop')) return '💻';
            return '🔌';
        }
        
        function formatMac(mac) {
            if (!mac) return '';
            
            // Если MAC адрес уже в формате "VR:DeviceName", просто возвращаем его как есть
            if (mac.startsWith('VR:')) {
                return mac;
            }
            
            // Если это обычный MAC адрес (6 байт в hex), форматируем его
            const cleanMac = mac.replace(/:/g, '').toUpperCase();
            if (cleanMac.length === 12) {
                return cleanMac.match(/.{1,2}/g).join(':');
            }
            
            // Если это какой-то другой формат, возвращаем как есть
            return mac;
        }
        
        function showSettings() {
            document.getElementById('settingsModal').style.display = 'block';
        }
        
        function closeModal(modalId) {
            document.getElementById(modalId).style.display = 'none';
        }
        
        function saveSettings() {
            const ssid = document.getElementById('settingsSsid').value.trim();
            const password = document.getElementById('settingsPassword').value.trim();
            const subnet = document.getElementById('settingsSubnet').value;
            
            if (!ssid || !password || !subnet) {
                alert('Please fill all fields');
                return;
            }
            
            if (password.length < 8) {
                alert('Password must be at least 8 characters long');
                return;
            }
            
            if (subnet < 1 || subnet > 254) {
                alert('Subnet must be between 1 and 254');
                return;
            }
            
            if (!confirm('Changing settings will restart access point! Continue?')) {
                return;
            }
            
            const formData = new FormData();
            formData.append('ssid', ssid);
            formData.append('password', password);
            formData.append('subnet', subnet);
            
            fetch('/api/settings', {
                method: 'POST',
                body: formData
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    alert('Settings saved! ESP will restart...');
                    setTimeout(() => {
                        window.location.reload();
                    }, 2000);
                } else {
                    alert('Error: ' + (data.message || 'Unknown error'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Error saving settings');
            });
        }
        
        function startAutoRefresh() {
            const btn = document.getElementById('autoRefreshBtn');
            
            if (autoRefreshInterval) {
                clearInterval(autoRefreshInterval);
                autoRefreshInterval = null;
                btn.innerHTML = 'Auto (5s)';
            } else {
                autoRefreshInterval = setInterval(refreshData, 5000);
                btn.innerHTML = 'Stop';
                refreshData();
            }
        }
        
        window.onclick = function(event) {
            const modals = document.getElementsByClassName('modal');
            for (let modal of modals) {
                if (event.target === modal) {
                    modal.style.display = 'none';
                }
            }
        }
        
        // Инициализация при загрузке страницы
        document.addEventListener('DOMContentLoaded', function() {
            refreshData();
            startAutoRefresh();
            // Подключаем WebSocket для реального обновления данных
            setTimeout(connectWebSocket, 1000);
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

// Функция для поиска устройства по IP
int findDeviceByIP(const String& ip) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].ip == ip) {
      return i;
    }
  }
  return -1;
}

// Функция для получения отображаемого имени устройства
String getDisplayName(const String& mac, const String& originalHostname) {
  return originalHostname.length() > 0 ? originalHostname : "Unknown";
}

// Функция для добавления нового устройства
void addDevice(const String& ip, const String& mac, const String& hostname, int rssi) {
  int index = findDeviceByMAC(mac);
  
  if (index == -1) {
    if (deviceCount < MAX_DEVICES) {
      devices[deviceCount].ip = ip;
      devices[deviceCount].mac = mac;
      devices[deviceCount].hostname = hostname;
      devices[deviceCount].rssi = rssi;
      devices[deviceCount].ipFixed = false;
      devices[deviceCount].hasMPU6050 = false;
      devices[deviceCount].pitch = 0;
      devices[deviceCount].roll = 0;
      devices[deviceCount].yaw = 0;
      devices[deviceCount].relPitch = 0;
      devices[deviceCount].relRoll = 0;
      devices[deviceCount].relYaw = 0;
      devices[deviceCount].lastUpdate = 0;
      devices[deviceCount].connected = true;
      deviceCount++;
      
      Serial.printf("New device: %s (%s) - %s - RSSI: %d\n", 
                   hostname.c_str(), ip.c_str(), mac.c_str(), rssi);
    }
  } else {
    devices[index].rssi = rssi;
    devices[index].ip = ip;
    devices[index].connected = true;
    if (hostname.length() > 0) {
      devices[index].hostname = hostname;
    }
  }
}

// Функция для обновления данных MPU6050 от VR-клиента
void updateVRDeviceData(const String& ip, const String& deviceName, 
                       float pitch, float roll, float yaw,
                       float relPitch, float relRoll, float relYaw) {
  int index = findDeviceByIP(ip);
  
  if (index == -1) {
    // Создаем новое устройство если не найдено
    if (deviceCount < MAX_DEVICES) {
      devices[deviceCount].ip = ip;
      devices[deviceCount].mac = "VR:" + deviceName;
      devices[deviceCount].hostname = deviceName;
      devices[deviceCount].rssi = -50;
      devices[deviceCount].ipFixed = false;
      devices[deviceCount].hasMPU6050 = true;
      devices[deviceCount].pitch = pitch;
      devices[deviceCount].roll = roll;
      devices[deviceCount].yaw = yaw;
      devices[deviceCount].relPitch = relPitch;
      devices[deviceCount].relRoll = relRoll;
      devices[deviceCount].relYaw = relYaw;
      devices[deviceCount].lastUpdate = millis();
      devices[deviceCount].connected = true;
      deviceCount++;
      
      Serial.printf("New VR device: %s (%s) - Pitch: %.1f, Roll: %.1f, Yaw: %.1f\n", 
                   deviceName.c_str(), ip.c_str(), pitch, roll, yaw);
    }
  } else {
    // Обновляем существующее устройство
    devices[index].hasMPU6050 = true;
    devices[index].pitch = pitch;
    devices[index].roll = roll;
    devices[index].yaw = yaw;
    devices[index].relPitch = relPitch;
    devices[index].relRoll = relRoll;
    devices[index].relYaw = relYaw;
    devices[index].lastUpdate = millis();
    devices[index].connected = true;
    
    if (deviceName.length() > 0) {
      devices[index].hostname = deviceName;
    }
    
    // Отправляем обновление через WebSocket всем веб-клиентам
    DynamicJsonDocument doc(512);
    doc["type"] = "sensor_update";
    JsonObject deviceObj = doc.createNestedObject("device");
    deviceObj["ip"] = devices[index].ip;
    deviceObj["hasMPU6050"] = devices[index].hasMPU6050;
    deviceObj["pitch"] = devices[index].pitch;
    deviceObj["roll"] = devices[index].roll;
    deviceObj["yaw"] = devices[index].yaw;
    deviceObj["relPitch"] = devices[index].relPitch;
    deviceObj["relRoll"] = devices[index].relRoll;
    deviceObj["relYaw"] = devices[index].relYaw;
    
    String json;
    serializeJson(doc, json);
    webSocketWeb.broadcastTXT(json);
    
    Serial.printf("VR data updated for %s: Pitch=%.1f, Roll=%.1f, Yaw=%.1f\n", 
                 ip.c_str(), pitch, roll, yaw);
  }
}

// Функция для сканирования сети
void scanNetwork() {
  struct station_info *station = wifi_softap_get_station_info();
  int onlineCount = 0;
  
  // Помечаем все устройства как отключенные
  for (int i = 0; i < deviceCount; i++) {
    devices[i].connected = false;
  }
  
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
  
  // Помечаем VR-устройства как онлайн если они обновлялись недавно
  unsigned long currentTime = millis();
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].hasMPU6050 && (currentTime - devices[i].lastUpdate) < 30000) {
      devices[i].connected = true;
      onlineCount++;
    }
  }
  
  Serial.printf("Scan complete. Found %d online devices\n", onlineCount);
  lastScanTime = millis();
}

// Функция для сохранения настроек сети в EEPROM
void saveNetworkSettingsToEEPROM() {
  EEPROM.begin(512);
  
  int addr = 0;
  EEPROM.write(addr++, networkSettings.configured ? 1 : 0);
  
  // Сохраняем SSID
  EEPROM.write(addr++, networkSettings.ssid.length());
  for (int j = 0; j < networkSettings.ssid.length(); j++) {
    EEPROM.write(addr++, networkSettings.ssid[j]);
  }
  
  // Сохраняем пароль
  EEPROM.write(addr++, networkSettings.password.length());
  for (int j = 0; j < networkSettings.password.length(); j++) {
    EEPROM.write(addr++, networkSettings.password[j]);
  }
  
  // Сохраняем подсеть
  EEPROM.write(addr++, networkSettings.subnet.length());
  for (int j = 0; j < networkSettings.subnet.length(); j++) {
    EEPROM.write(addr++, networkSettings.subnet[j]);
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("Network settings saved to EEPROM");
}

// Функция для загрузки настроек сети из EEPROM
void loadNetworkSettingsFromEEPROM() {
  EEPROM.begin(512);
  
  int addr = 0;
  networkSettings.configured = (EEPROM.read(addr++) == 1);
  
  if (networkSettings.configured) {
    // Загружаем SSID
    int ssidLen = EEPROM.read(addr++);
    networkSettings.ssid = "";
    for (int j = 0; j < ssidLen; j++) {
      networkSettings.ssid += char(EEPROM.read(addr++));
    }
    
    // Загружаем пароль
    int passwordLen = EEPROM.read(addr++);
    networkSettings.password = "";
    for (int j = 0; j < passwordLen; j++) {
      networkSettings.password += char(EEPROM.read(addr++));
    }
    
    // Загружаем подсеть
    int subnetLen = EEPROM.read(addr++);
    networkSettings.subnet = "";
    for (int j = 0; j < subnetLen; j++) {
      networkSettings.subnet += char(EEPROM.read(addr++));
    }
  } else {
    // Значения по умолчанию
    networkSettings.ssid = ap_ssid;
    networkSettings.password = ap_password;
    networkSettings.subnet = "50";
  }
  
  EEPROM.end();
  
  Serial.printf("Network settings loaded: SSID=%s, Subnet=%s\n", 
                networkSettings.ssid.c_str(), networkSettings.subnet.c_str());
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

// Обработчик WebSocket событий для веб-клиентов
void webSocketWebEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WEB %u] Web Client Disconnected!\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocketWeb.remoteIP(num);
        Serial.printf("[WEB %u] Web Client Connected from %s\n", num, ip.toString().c_str());
        
        // Отправляем приветственное сообщение
        String welcomeMsg = "{\"type\":\"connected\",\"message\":\"Real-time updates enabled\"}";
        webSocketWeb.sendTXT(num, welcomeMsg);
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        Serial.printf("[WEB %u] Received: %s\n", num, message.c_str());
        
        // Обработка команд от веб-клиентов
        if (message == "get_devices") {
          // Можно добавить отправку текущего состояния устройств
        }
      }
      break;
      
    case WStype_ERROR:
      Serial.printf("[WEB %u] WebSocket error\n", num);
      break;
  }
}

// Функция для парсинга данных MPU6050 из строки
bool parseMPU6050Data(const String& message, String& deviceName, 
                     float& pitch, float& roll, float& yaw,
                     float& relPitch, float& relRoll, float& relYaw) {
  // Пример формата: "DEVICE:VR-Head-Hom-001,PITCH:13.3,ROLL:5.7,YAW:-9.7,REL_PITCH:13.30,REL_ROLL:5.68,REL_YAW:-9.70,ACC_PITCH:13.30,ACC_ROLL:5.68,ACC_YAW:-9.70,ZERO_SET:false,TIMESTAMP:237540"
  
  if (!message.startsWith("DEVICE:")) {
    return false;
  }
  
  // Разбиваем сообщение на части
  int startPos = 7; // После "DEVICE:"
  int endPos = message.indexOf(',', startPos);
  if (endPos == -1) return false;
  
  deviceName = message.substring(startPos, endPos);
  
  // Парсим основные значения
  pitch = extractValue(message, "PITCH:");
  roll = extractValue(message, "ROLL:");
  yaw = extractValue(message, "YAW:");
  relPitch = extractValue(message, "REL_PITCH:");
  relRoll = extractValue(message, "REL_ROLL:");
  relYaw = extractValue(message, "REL_YAW:");
  
  return true;
}

// Вспомогательная функция для извлечения значения из строки
float extractValue(const String& message, const String& key) {
  int keyPos = message.indexOf(key);
  if (keyPos == -1) return 0.0;
  
  int valueStart = keyPos + key.length();
  int valueEnd = message.indexOf(',', valueStart);
  if (valueEnd == -1) valueEnd = message.length();
  
  String valueStr = message.substring(valueStart, valueEnd);
  return valueStr.toFloat();
}

// Обработчик WebSocket событий для VR-клиентов
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[VR %u] VR Client Disconnected!\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[VR %u] VR Client Connected from %s\n", num, ip.toString().c_str());
        
        // Отправляем приветственное сообщение
        String welcomeMsg = "VR_SERVER:CONNECTED";
        webSocket.sendTXT(num, welcomeMsg);
      }
      break;
      
    case WStype_TEXT:
      {
        String message = String((char*)payload);
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[VR %u] Received from VR Client: %s\n", num, message.c_str());
        
        // Обработка данных от VR-клиента
        if (message.startsWith("DEVICE:")) {
          String deviceName = "";
          float pitch = 0, roll = 0, yaw = 0;
          float relPitch = 0, relRoll = 0, relYaw = 0;
          
          // Парсим данные MPU6050
          if (parseMPU6050Data(message, deviceName, pitch, roll, yaw, relPitch, relRoll, relYaw)) {
            // Обновляем данные устройства
            updateVRDeviceData(ip.toString(), deviceName, pitch, roll, yaw, relPitch, relRoll, relYaw);
            
            // Отправляем подтверждение
            String ackMsg = "DATA_RECEIVED";
            webSocket.sendTXT(num, ackMsg);
          } else {
            Serial.println("Error parsing MPU6050 data");
          }
        }
        else if (message == "PING") {
          String pongMsg = "PONG";
          webSocket.sendTXT(num, pongMsg);
        }
        else if (message.startsWith("DEVICE_CONNECTED:")) {
          // Обработка подключения нового устройства
          String deviceName = message.substring(17);
          Serial.printf("VR Device registered: %s from %s\n", deviceName.c_str(), ip.toString().c_str());
          String welcomeDeviceMsg = "WELCOME:" + deviceName;
          webSocket.sendTXT(num, welcomeDeviceMsg);
        }
        else if (message == "CALIBRATION_COMPLETE") {
          Serial.printf("[VR %u] Calibration completed\n", num);
          // Можно добавить дополнительную логику при завершении калибровки
        }
        else if (message == "ANGLES_RESET") {
          Serial.printf("[VR %u] Angles reset to zero\n", num);
          // Можно добавить дополнительную логику при сбросе углов
        }
      }
      break;
      
    case WStype_ERROR:
      Serial.printf("[VR %u] WebSocket error\n", num);
      break;
  }
}

// Настройка WiFi в режиме точки доступа
void setupWiFiAP() {
  Serial.println("Setting up Access Point...");
  
  WiFi.mode(WIFI_AP);
  
  // Конвертируем подсеть в число
  int subnet = networkSettings.subnet.toInt();
  if (subnet < 1 || subnet > 254) {
    subnet = 50; // Значение по умолчанию при ошибке
  }
  
  IPAddress local_ip(192, 168, subnet, 1);
  IPAddress gateway(192, 168, subnet, 1);
  IPAddress subnet_mask(255, 255, 255, 0);
  
  WiFi.softAP(networkSettings.ssid.c_str(), networkSettings.password.c_str());
  WiFi.softAPConfig(local_ip, gateway, subnet_mask);
  
  Serial.println("Access Point Started");
  Serial.print("SSID: "); Serial.println(networkSettings.ssid);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
}

// Обработчик главной страницы
void handleRoot() {
  Serial.println("Serving HTML page...");
  server.send_P(200, "text/html", htmlPage);
}

// API для получения списка устройств
void handleApiDevices() {
  Serial.println("API devices requested");
  
  DynamicJsonDocument doc(2048);
  
  // Создаем массив для хранения уникальных устройств
  DeviceInfo uniqueDevices[MAX_DEVICES];
  int uniqueCount = 0;
  
  // Сначала проходим по всем устройствам и собираем устройства с MPU6050
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].hasMPU6050) {
      bool exists = false;
      for (int j = 0; j < uniqueCount; j++) {
        if (uniqueDevices[j].ip == devices[i].ip) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        uniqueDevices[uniqueCount++] = devices[i];
      }
    }
  }
  
  // Затем добавляем устройства без MPU6050
  for (int i = 0; i < deviceCount; i++) {
    if (!devices[i].hasMPU6050) {
      bool exists = false;
      for (int j = 0; j < uniqueCount; j++) {
        if (uniqueDevices[j].ip == devices[i].ip) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        uniqueDevices[uniqueCount++] = devices[i];
      }
    }
  }
  
  // Подсчет онлайн устройств и VR-устройств среди уникальных
  int onlineCount = 0;
  int vrCount = 0;
  for (int i = 0; i < uniqueCount; i++) {
    if (uniqueDevices[i].connected) {
      onlineCount++;
    }
    if (uniqueDevices[i].hasMPU6050) {
      vrCount++;
    }
  }
  
  doc["totalDevices"] = uniqueCount;
  doc["onlineDevices"] = onlineCount;
  doc["vrDevices"] = vrCount;
  doc["espIp"] = WiFi.softAPIP().toString();
  
  JsonArray devicesArray = doc.createNestedArray("devices");
  
  for (int i = 0; i < uniqueCount; i++) {
    JsonObject deviceObj = devicesArray.createNestedObject();
    deviceObj["ip"] = uniqueDevices[i].ip;
    deviceObj["mac"] = uniqueDevices[i].mac;
    deviceObj["hostname"] = uniqueDevices[i].hostname;
    deviceObj["displayName"] = getDisplayName(uniqueDevices[i].mac, uniqueDevices[i].hostname);
    deviceObj["rssi"] = uniqueDevices[i].rssi;
    deviceObj["ipFixed"] = uniqueDevices[i].ipFixed;
    deviceObj["hasMPU6050"] = uniqueDevices[i].hasMPU6050;
    deviceObj["connected"] = uniqueDevices[i].connected;
    
    if (uniqueDevices[i].hasMPU6050) {
      deviceObj["pitch"] = uniqueDevices[i].pitch;
      deviceObj["roll"] = uniqueDevices[i].roll;
      deviceObj["yaw"] = uniqueDevices[i].yaw;
      deviceObj["relPitch"] = uniqueDevices[i].relPitch;
      deviceObj["relRoll"] = uniqueDevices[i].relRoll;
      deviceObj["relYaw"] = uniqueDevices[i].relYaw;
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// API для калибровки датчика
void handleApiCalibrate() {
  if (server.method() == HTTP_POST) {
    String ip = server.arg("ip");
    Serial.printf("Calibration request for device: %s\n", ip.c_str());
    
    // Находим устройство по IP
    int deviceIndex = findDeviceByIP(ip);
    if (deviceIndex != -1 && devices[deviceIndex].hasMPU6050) {
      // Отправляем команду калибровки через WebSocket
      bool commandSent = false;
      for (int i = 0; i < webSocket.connectedClients(); i++) {
        IPAddress clientIp = webSocket.remoteIP(i);
        if (clientIp.toString() == ip) {
          webSocket.sendTXT(i, "CALIBRATE_SENSOR");
          commandSent = true;
          Serial.printf("Calibration command sent to %s\n", ip.c_str());
          break;
        }
      }
      
      if (commandSent) {
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Calibration command sent\"}");
      } else {
        server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"VR device not connected\"}");
      }
    } else {
      server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Device not found or not a VR device\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для сброса углов
void handleApiReset() {
  if (server.method() == HTTP_POST) {
    String ip = server.arg("ip");
    Serial.printf("Reset angles request for device: %s\n", ip.c_str());
    
    // Находим устройство по IP
    int deviceIndex = findDeviceByIP(ip);
    if (deviceIndex != -1 && devices[deviceIndex].hasMPU6050) {
      // Отправляем команду сброса через WebSocket
      bool commandSent = false;
      for (int i = 0; i < webSocket.connectedClients(); i++) {
        IPAddress clientIp = webSocket.remoteIP(i);
        if (clientIp.toString() == ip) {
          webSocket.sendTXT(i, "RESET_ANGLES");
          commandSent = true;
          Serial.printf("Reset angles command sent to %s\n", ip.c_str());
          break;
        }
      }
      
      if (commandSent) {
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Reset command sent\"}");
      } else {
        server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"VR device not connected\"}");
      }
    } else {
      server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Device not found or not a VR device\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для сохранения настроек
void handleApiSettings() {
  if (server.method() == HTTP_POST) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String subnet = server.arg("subnet");
    
    Serial.printf("Settings request - SSID: %s, Subnet: %s\n", ssid.c_str(), subnet.c_str());
    
    if (ssid.length() > 0 && password.length() >= 8 && subnet.length() > 0) {
      // Сохраняем новые настройки
      networkSettings.ssid = ssid;
      networkSettings.password = password;
      networkSettings.subnet = subnet;
      networkSettings.configured = true;
      
      saveNetworkSettingsToEEPROM();
      
      server.send(200, "application/json", "{\"status\":\"success\"}");
      Serial.println("Settings saved successfully");
      
      // Перезагрузка для применения новых настроек
      delay(1000);
      ESP.restart();
      
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters - check SSID, password (min 8 chars) and subnet\"}");
      Serial.println("Error: Invalid parameters");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
    Serial.println("Error: Method not allowed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nStarting VR Tracking Server...");
  
  // Очистка EEPROM при первом запуске (раскомментировать для очистки)
  // clearEEPROM();
  
  // Загрузка настроек сети из EEPROM
  loadNetworkSettingsFromEEPROM();
  
  // Настройка WiFi
  setupWiFiAP();
  
  // Настройка маршрутов сервера
  server.on("/", handleRoot);
  server.on("/api/devices", handleApiDevices);
  server.on("/api/calibrate", HTTP_POST, handleApiCalibrate);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  
  // Запуск сервера
  server.begin();
  
  // Запуск WebSocket сервера для VR-клиентов
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Запуск WebSocket сервера для веб-клиентов
  webSocketWeb.begin();
  webSocketWeb.onEvent(webSocketWebEvent);
  
  Serial.println("HTTP server started on port 80");
  Serial.println("WebSocket server for VR clients started on port 81");
  Serial.println("WebSocket server for web clients started on port 82");
  Serial.println("Ready to receive MPU6050 data from VR headsets!");
  
  // Первое сканирование
  scanNetwork();
  
  Serial.println("Setup completed successfully!");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  webSocketWeb.loop();
  
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    scanNetwork();
  }
  
  delay(100);
}
