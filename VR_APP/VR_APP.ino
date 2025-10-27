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

// Структура для фиксации IP адреса
struct FixedIP {
  char mac[18];
  char ip[16];
};

// Структура для хранения информации об устройстве
struct DeviceInfo {
  char ip[16];
  char mac[18];
  char originalMac[18];  // Добавлено для отображения оригинального MAC
  char hostname[32];
  char customName[32];
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
  char ssid[32];
  char password[32];
  char subnet[4];
  bool configured;
};

// Структура для хранения пользовательских имен устройств
struct DeviceAlias {
  char mac[18];
  char alias[32];
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 20;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Массив для хранения пользовательских имен
const int MAX_ALIASES = 30;
DeviceAlias deviceAliases[MAX_ALIASES];
int aliasCount = 0;

// Массив для фиксированных IP адресов
const int MAX_FIXED_IPS = 20;
FixedIP fixedIPs[MAX_FIXED_IPS];
int fixedIPCount = 0;

// Настройки сети
NetworkSettings networkSettings;

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// Время последней записи в EEPROM
unsigned long lastEEPROMSave = 0;
const unsigned long EEPROM_SAVE_INTERVAL = 5000;
bool eepromDirty = false;

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
        .device-card.ip-fixed { border-right: 4px solid #27ae60; background: #f8fff8; }
        .device-header { display: flex; justify-content: space-between; margin-bottom: 6px; }
        .device-name { font-weight: bold; color: #2c3e50; font-size: 14px; cursor: pointer; }
        .device-name:hover { color: #3498db; text-decoration: underline; }
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
        .ip-controls { display: flex; gap: 5px; margin-top: 5px; }
        .ip-btn { padding: 3px 8px; border: none; border-radius: 3px; background: #9b59b6; color: white; cursor: pointer; font-size: 9px; flex: 1; }
        .ip-btn.fix { background: #27ae60; }
        .ip-btn.unfix { background: #e67e22; }
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
        .fixed-badge { background: #27ae60; color: white; padding: 1px 4px; border-radius: 3px; font-size: 8px; margin-left: 5px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1 style="margin:0;font-size:20px;">🎮 VR Tracking Server</h1>
            <p style="margin:5px 0 0 0;font-size:12px;">Real-time MPU6050 device monitoring with IP management</p>
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
                <div class="stat-number" id="fixedIPs">0</div>
                <div class="stat-label">Fixed IPs</div>
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

    <!-- Modal for device name editing -->
    <div id="renameModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <div class="modal-title">✏️ Rename Device</div>
                <span class="close" onclick="closeModal('renameModal')">&times;</span>
            </div>
            <div class="form-group">
                <label class="form-label">MAC Address:</label>
                <input type="text" id="renameMac" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">Current Name:</label>
                <input type="text" id="renameCurrent" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">New Name:</label>
                <input type="text" id="renameNew" class="form-input" maxlength="30" placeholder="Enter custom name">
            </div>
            <div class="modal-buttons">
                <button class="btn" onclick="closeModal('renameModal')">Cancel</button>
                <button class="btn btn-settings" onclick="saveDeviceName()">💾 Save Name</button>
            </div>
        </div>
    </div>

    <!-- Modal for IP management -->
    <div id="ipModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <div class="modal-title">🌐 Manage IP Address</div>
                <span class="close" onclick="closeModal('ipModal')">&times;</span>
            </div>
            <div class="form-group">
                <label class="form-label">Device Name:</label>
                <input type="text" id="ipDeviceName" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">MAC Address:</label>
                <input type="text" id="ipMac" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">Current IP:</label>
                <input type="text" id="ipCurrent" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">Fixed IP (2-254):</label>
                <input type="number" id="ipFixed" class="form-input" min="2" max="254" placeholder="Enter fixed IP">
            </div>
            <div class="modal-buttons">
                <button class="btn" onclick="closeModal('ipModal')">Cancel</button>
                <button class="btn" onclick="unfixIP()" id="unfixBtn">🔓 Unfix IP</button>
                <button class="btn btn-settings" onclick="fixIP()">🔒 Fix IP</button>
            </div>
        </div>
    </div>

    <script>
        let autoRefreshInterval = null;
        let webSocketClient = null;
        let currentEditMac = '';
        let currentIPMac = '';
        
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
        
        function editDeviceName(mac, currentName) {
            currentEditMac = mac;
            document.getElementById('renameMac').value = mac;
            document.getElementById('renameCurrent').value = currentName;
            document.getElementById('renameNew').value = currentName;
            document.getElementById('renameModal').style.display = 'block';
            document.getElementById('renameNew').focus();
        }
        
        function manageIP(mac, deviceName, currentIP, isFixed) {
            currentIPMac = mac;
            document.getElementById('ipDeviceName').value = deviceName;
            document.getElementById('ipMac').value = mac;
            document.getElementById('ipCurrent').value = currentIP;
            
            if (isFixed) {
                const ipParts = currentIP.split('.');
                document.getElementById('ipFixed').value = ipParts[3];
                document.getElementById('unfixBtn').style.display = 'inline-block';
            } else {
                document.getElementById('ipFixed').value = '';
                document.getElementById('unfixBtn').style.display = 'none';
            }
            
            document.getElementById('ipModal').style.display = 'block';
            document.getElementById('ipFixed').focus();
        }
        
        function fixIP() {
            const fixedIP = document.getElementById('ipFixed').value;
            const mac = currentIPMac;
            
            if (!fixedIP || fixedIP < 2 || fixedIP > 254) {
                alert('Please enter a valid IP address (2-254)');
                return;
            }
            
            fetch('/api/fixip', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'mac=' + encodeURIComponent(mac) + '&ip=' + encodeURIComponent(fixedIP)
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    closeModal('ipModal');
                    refreshData();
                    alert('IP address fixed successfully! Device needs to reconnect.');
                } else {
                    alert('Ошибка: ' + (data.message || 'Неизвестная ошибка'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Ошибка фиксации IP адреса');
            });
        }
        
        function unfixIP() {
            const mac = currentIPMac;
            
            if (!confirm('Unfix IP address for this device?')) {
                return;
            }
            
            fetch('/api/unfixip', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'mac=' + encodeURIComponent(mac)
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    closeModal('ipModal');
                    refreshData();
                    alert('IP address unfixed successfully!');
                } else {
                    alert('Ошибка: ' + (data.message || 'Неизвестная ошибка'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Ошибка снятия фиксации IP адреса');
            });
        }
        
        function saveDeviceName() {
            const newName = document.getElementById('renameNew').value.trim();
            const mac = currentEditMac;
            
            if (!newName) {
                alert('Please enter a name');
                return;
            }
            
            if (newName.length > 30) {
                alert('Name too long (max 30 characters)');
                return;
            }
            
            fetch('/api/rename', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: 'mac=' + encodeURIComponent(mac) + '&name=' + encodeURIComponent(newName)
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    closeModal('renameModal');
                    refreshData(); // Обновляем отображение
                } else {
                    alert('Ошибка: ' + (data.message || 'Неизвестная ошибка'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Ошибка сохранения имени');
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
            document.getElementById('fixedIPs').textContent = data.fixedIPs;
            document.getElementById('espIp').textContent = data.espIp;
            document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();
            
            const container = document.getElementById('devicesContainer');
            
            if (!data.devices || data.devices.length === 0) {
                container.innerHTML = '<div class="no-devices">No devices found on network</div>';
                return;
            }
            
            container.innerHTML = data.devices.map(device => `
                <div class="device-card ${device.hasMPU6050 ? 'vr-device' : ''} ${device.ipFixed ? 'ip-fixed' : ''}" data-ip="${device.ip}">
                    <div class="device-header">
                        <div class="device-name" onclick="editDeviceName('${device.mac}', '${device.displayName.replace(/'/g, "\\'")}')">
                            ${getDeviceIcon(device)} ${device.displayName}
                            ${device.hasMPU6050 ? '<span class="vr-badge">VR</span>' : ''}
                            ${device.ipFixed ? '<span class="fixed-badge">FIXED</span>' : ''}
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
                        <span class="info-label">Original MAC:</span>
                        <span class="info-value">${formatMac(device.originalMac)}</span>
                    </div>
                    <div class="info-row">
                        <span class="info-label">Signal:</span>
                        <span class="info-value">${device.rssi} dBm</span>
                    </div>
                    <div class="ip-controls">
                        <button class="ip-btn fix" onclick="manageIP('${device.mac}', '${device.displayName.replace(/'/g, "\\'")}', '${device.ip}', ${device.ipFixed})">
                            ${device.ipFixed ? 'Change Fixed IP' : 'Fix IP Address'}
                        </button>
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

// Функция для безопасного копирования строк
void safeStrcpy(char* dest, const char* src, size_t destSize) {
  if (destSize > 0) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
  }
}

// Функция для поиска устройства в массиве
int findDeviceByMAC(const char* mac) {
  for (int i = 0; i < deviceCount; i++) {
    if (strcmp(devices[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска устройства по IP
int findDeviceByIP(const char* ip) {
  for (int i = 0; i < deviceCount; i++) {
    if (strcmp(devices[i].ip, ip) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска алиаса по MAC
int findAliasByMAC(const char* mac) {
  for (int i = 0; i < aliasCount; i++) {
    if (strcmp(deviceAliases[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска фиксированного IP по MAC
int findFixedIPByMAC(const char* mac) {
  for (int i = 0; i < fixedIPCount; i++) {
    if (strcmp(fixedIPs[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для получения отображаемого имени устройства
String getDisplayName(const char* mac, const char* originalHostname) {
  // Сначала ищем пользовательское имя
  int aliasIndex = findAliasByMAC(mac);
  if (aliasIndex != -1) {
    return String(deviceAliases[aliasIndex].alias);
  }
  
  // Если пользовательского имени нет, используем оригинальное
  return originalHostname && strlen(originalHostname) > 0 ? String(originalHostname) : "Unknown";
}

// Функция для добавления нового устройства
void addDevice(const char* ip, const char* mac, const char* hostname, int rssi) {
  int index = findDeviceByMAC(mac);
  
  if (index == -1) {
    if (deviceCount < MAX_DEVICES) {
      safeStrcpy(devices[deviceCount].ip, ip, sizeof(devices[deviceCount].ip));
      safeStrcpy(devices[deviceCount].mac, mac, sizeof(devices[deviceCount].mac));
      safeStrcpy(devices[deviceCount].originalMac, mac, sizeof(devices[deviceCount].originalMac)); // Сохраняем оригинальный MAC
      safeStrcpy(devices[deviceCount].hostname, hostname, sizeof(devices[deviceCount].hostname));
      safeStrcpy(devices[deviceCount].customName, "", sizeof(devices[deviceCount].customName));
      devices[deviceCount].rssi = rssi;
      
      // Проверяем есть ли фиксированный IP для этого MAC
      int fixedIndex = findFixedIPByMAC(mac);
      devices[deviceCount].ipFixed = (fixedIndex != -1);
      
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
      
      Serial.printf("New device: %s (%s) - %s - RSSI: %d - IP Fixed: %s\n", 
                   hostname, ip, mac, rssi, devices[deviceCount-1].ipFixed ? "YES" : "NO");
    } else {
      Serial.println("Device limit reached!");
    }
  } else {
    devices[index].rssi = rssi;
    safeStrcpy(devices[index].ip, ip, sizeof(devices[index].ip));
    devices[index].connected = true;
    if (hostname && strlen(hostname) > 0) {
      safeStrcpy(devices[index].hostname, hostname, sizeof(devices[index].hostname));
    }
  }
}

// Функция для обновления данных MPU6050 от VR-клиента
void updateVRDeviceData(const char* ip, const char* deviceName, 
                       float pitch, float roll, float yaw,
                       float relPitch, float relRoll, float relYaw) {
  int index = findDeviceByIP(ip);
  
  if (index == -1) {
    // Создаем новое устройство если не найдено
    if (deviceCount < MAX_DEVICES) {
      safeStrcpy(devices[deviceCount].ip, ip, sizeof(devices[deviceCount].ip));
      
      char vrMac[32];
      snprintf(vrMac, sizeof(vrMac), "VR:%s", deviceName);
      safeStrcpy(devices[deviceCount].mac, vrMac, sizeof(devices[deviceCount].mac));
      safeStrcpy(devices[deviceCount].originalMac, vrMac, sizeof(devices[deviceCount].originalMac));
      
      safeStrcpy(devices[deviceCount].hostname, deviceName, sizeof(devices[deviceCount].hostname));
      safeStrcpy(devices[deviceCount].customName, "", sizeof(devices[deviceCount].customName));
      // devices[deviceCount].rssi = -50;
      devices[deviceCount].ipFixed = false; // VR устройства не фиксируем по IP
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
                   deviceName, ip, pitch, roll, yaw);
    } else {
      Serial.println("Device limit reached! Cannot add new VR device");
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
    
    if (deviceName && strlen(deviceName) > 0) {
      safeStrcpy(devices[index].hostname, deviceName, sizeof(devices[index].hostname));
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
    Serial.printf("VR data updated for %s: Pitch=%.1f, Roll=%.1f, Yaw=%.1f\n", ip, pitch, roll, yaw);
  }
}

// Упрощенная функция сканирования сети - возвращает тестовые данные
void scanNetwork() {
  Serial.println("Starting network scan...");
  
  // Помечаем все устройства как отключенные
  for (int i = 0; i < deviceCount; i++) {
    devices[i].connected = false;
  }
  
  // Тестовые данные для демонстрации
  // В реальной системе здесь должен быть код для сканирования сети
  // addDevice("192.168.4.2", "aa:bb:cc:dd:ee:ff", "Test-Device-1", -45);
  // addDevice("192.168.4.3", "11:22:33:44:55:66", "Test-Phone", -55);
  
  // Помечаем VR-устройства как онлайн если они обновлялись недавно
  unsigned long currentTime = millis();
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].hasMPU6050 && (currentTime - devices[i].lastUpdate) < 30000) {
      devices[i].connected = true;
    }
  }
  
  Serial.printf("Scan complete. Found %d devices\n", deviceCount);
  lastScanTime = millis();
}

// Функция для сохранения настроек сети в EEPROM
void saveNetworkSettingsToEEPROM() {
  EEPROM.begin(2048); // Увеличиваем размер для фиксированных IP
  
  int addr = 0;
  EEPROM.write(addr++, networkSettings.configured ? 1 : 0);
  
  // Сохраняем SSID
  EEPROM.write(addr++, strlen(networkSettings.ssid));
  for (size_t j = 0; j < strlen(networkSettings.ssid); j++) {
    EEPROM.write(addr++, networkSettings.ssid[j]);
  }
  
  // Сохраняем пароль
  EEPROM.write(addr++, strlen(networkSettings.password));
  for (size_t j = 0; j < strlen(networkSettings.password); j++) {
    EEPROM.write(addr++, networkSettings.password[j]);
  }
  
  // Сохраняем подсеть
  EEPROM.write(addr++, strlen(networkSettings.subnet));
  for (size_t j = 0; j < strlen(networkSettings.subnet); j++) {
    EEPROM.write(addr++, networkSettings.subnet[j]);
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("Network settings saved to EEPROM");
}

// Функция для загрузки настроек сети из EEPROM
void loadNetworkSettingsFromEEPROM() {
  EEPROM.begin(2048);
  
  int addr = 0;
  networkSettings.configured = (EEPROM.read(addr++) == 1);
  
  if (networkSettings.configured) {
    // Загружаем SSID
    int ssidLen = EEPROM.read(addr++);
    for (int j = 0; j < ssidLen && j < 31; j++) {
      networkSettings.ssid[j] = EEPROM.read(addr++);
    }
    networkSettings.ssid[ssidLen] = '\0';
    
    // Загружаем пароль
    int passwordLen = EEPROM.read(addr++);
    for (int j = 0; j < passwordLen && j < 31; j++) {
      networkSettings.password[j] = EEPROM.read(addr++);
    }
    networkSettings.password[passwordLen] = '\0';
    
    // Загружаем подсеть
    int subnetLen = EEPROM.read(addr++);
    for (int j = 0; j < subnetLen && j < 3; j++) {
      networkSettings.subnet[j] = EEPROM.read(addr++);
    }
    networkSettings.subnet[subnetLen] = '\0';
  } else {
    // Значения по умолчанию
    safeStrcpy(networkSettings.ssid, ap_ssid, sizeof(networkSettings.ssid));
    safeStrcpy(networkSettings.password, ap_password, sizeof(networkSettings.password));
    safeStrcpy(networkSettings.subnet, "50", sizeof(networkSettings.subnet));
  }
  
  EEPROM.end();
  
  Serial.printf("Network settings loaded: SSID=%s, Subnet=%s\n", 
                networkSettings.ssid, networkSettings.subnet);
}

// Функция для сохранения алиасов устройств в EEPROM
void saveAliasesToEEPROM() {
  EEPROM.begin(2048);
  
  int addr = 512; // Начинаем после сетевых настроек
  EEPROM.write(addr++, aliasCount);
  
  for (int i = 0; i < aliasCount; i++) {
    // Сохраняем MAC
    EEPROM.write(addr++, strlen(deviceAliases[i].mac));
    for (size_t j = 0; j < strlen(deviceAliases[i].mac); j++) {
      EEPROM.write(addr++, deviceAliases[i].mac[j]);
    }
    
    // Сохраняем алиас
    EEPROM.write(addr++, strlen(deviceAliases[i].alias));
    for (size_t j = 0; j < strlen(deviceAliases[i].alias); j++) {
      EEPROM.write(addr++, deviceAliases[i].alias[j]);
    }
  }
  
  // Сохраняем фиксированные IP
  addr = 1024; // Отдельная область для фиксированных IP
  EEPROM.write(addr++, fixedIPCount);
  
  for (int i = 0; i < fixedIPCount; i++) {
    // Сохраняем MAC
    EEPROM.write(addr++, strlen(fixedIPs[i].mac));
    for (size_t j = 0; j < strlen(fixedIPs[i].mac); j++) {
      EEPROM.write(addr++, fixedIPs[i].mac[j]);
    }
    
    // Сохраняем IP
    EEPROM.write(addr++, strlen(fixedIPs[i].ip));
    for (size_t j = 0; j < strlen(fixedIPs[i].ip); j++) {
      EEPROM.write(addr++, fixedIPs[i].ip[j]);
    }
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.println("Device aliases and fixed IPs saved to EEPROM");
  eepromDirty = false;
}

// Функция для загрузки алиасов устройств из EEPROM
void loadAliasesFromEEPROM() {
  EEPROM.begin(2048);
  
  int addr = 512;
  aliasCount = EEPROM.read(addr++);
  
  if (aliasCount > MAX_ALIASES) {
    aliasCount = 0;
  }
  
  for (int i = 0; i < aliasCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    for (int j = 0; j < macLen && j < 17; j++) {
      deviceAliases[i].mac[j] = EEPROM.read(addr++);
    }
    deviceAliases[i].mac[macLen] = '\0';
    
    // Загружаем алиас
    int aliasLen = EEPROM.read(addr++);
    for (int j = 0; j < aliasLen && j < 31; j++) {
      deviceAliases[i].alias[j] = EEPROM.read(addr++);
    }
    deviceAliases[i].alias[aliasLen] = '\0';
  }
  
  // Загружаем фиксированные IP
  addr = 1024;
  fixedIPCount = EEPROM.read(addr++);
  
  if (fixedIPCount > MAX_FIXED_IPS) {
    fixedIPCount = 0;
  }
  
  for (int i = 0; i < fixedIPCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    for (int j = 0; j < macLen && j < 17; j++) {
      fixedIPs[i].mac[j] = EEPROM.read(addr++);
    }
    fixedIPs[i].mac[macLen] = '\0';
    
    // Загружаем IP
    int ipLen = EEPROM.read(addr++);
    for (int j = 0; j < ipLen && j < 15; j++) {
      fixedIPs[i].ip[j] = EEPROM.read(addr++);
    }
    fixedIPs[i].ip[ipLen] = '\0';
  }
  
  EEPROM.end();
  
  Serial.printf("Loaded %d device aliases and %d fixed IPs from EEPROM\n", aliasCount, fixedIPCount);
}

// Функция для очистки EEPROM
void clearEEPROM() {
  EEPROM.begin(2048);
  for (int i = 0; i < 2048; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("EEPROM cleared successfully");
}

// Валидация WebSocket сообщений
bool isValidWebSocketMessage(const String& message) {
  // Проверяем длину сообщения
  if (message.length() == 0 || message.length() > 512) {
    return false;
  }
  
  // Проверяем на наличие недопустимых символов
  for (unsigned int i = 0; i < message.length(); i++) {
    char c = message[i];
    if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
      return false; // Управляющие символы
    }
    if (c > 126) {
      return false; // Не-ASCII символы
    }
  }
  
  return true;
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
        
        // Валидация входящего сообщения
        if (!isValidWebSocketMessage(message)) {
          Serial.printf("[WEB %u] Invalid WebSocket message received\n", num);
          webSocketWeb.sendTXT(num, "{\"type\":\"error\",\"message\":\"Invalid message\"}");
          return;
        }
        
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
bool parseMPU6050Data(const String& message, char* deviceName, 
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
  
  String deviceNameStr = message.substring(startPos, endPos);
  safeStrcpy(deviceName, deviceNameStr.c_str(), 32);
  
  // Парсим основные значения
  pitch = extractValue(message, "PITCH:");
  roll = extractValue(message, "ROLL:");
  yaw = extractValue(message, "YAW:");
  relPitch = extractValue(message, "REL_PITCH:");
  relRoll = extractValue(message, "REL_ROLL:");
  relYaw = extractValue(message, "REL_YAW:");
  
  return true;
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
        
        // Валидация входящего сообщения
        if (!isValidWebSocketMessage(message)) {
          Serial.printf("[VR %u] Invalid WebSocket message received\n", num);
          webSocket.sendTXT(num, "ERROR:INVALID_MESSAGE");
          return;
        }
        
        Serial.printf("[VR %u] Received from VR Client: %s\n", num, message.c_str());
        
        // Обработка данных от VR-клиента
        if (message.startsWith("DEVICE:")) {
          char deviceName[32] = "";
          float pitch = 0, roll = 0, yaw = 0;
          float relPitch = 0, relRoll = 0, relYaw = 0;
          
          // Парсим данные MPU6050
          if (parseMPU6050Data(message, deviceName, pitch, roll, yaw, relPitch, relRoll, relYaw)) {
            // Обновляем данные устройства
            updateVRDeviceData(ip.toString().c_str(), deviceName, pitch, roll, yaw, relPitch, relRoll, relYaw);
            
            // Отправляем подтверждение
            String ackMsg = "DATA_RECEIVED";
            webSocket.sendTXT(num, ackMsg);
          } else {
            Serial.println("Error parsing MPU6050 data");
            webSocket.sendTXT(num, "ERROR:PARSING_FAILED");
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
        else {
          webSocket.sendTXT(num, "ERROR:UNKNOWN_COMMAND");
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
  int subnet = atoi(networkSettings.subnet);
  if (subnet < 1 || subnet > 254) {
    subnet = 50; // Значение по умолчанию при ошибке
  }
  
  IPAddress local_ip(192, 168, subnet, 1);
  IPAddress gateway(192, 168, subnet, 1);
  IPAddress subnet_mask(255, 255, 255, 0);
  
  WiFi.softAP(networkSettings.ssid, networkSettings.password);
  WiFi.softAPConfig(local_ip, gateway, subnet_mask);
  
  Serial.println("Access Point Started");
  Serial.print("SSID: "); Serial.println(networkSettings.ssid);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
  Serial.printf("Subnet: 192.168.%d.0/24\n", subnet);
}

// Обработчик главной страницы
void handleRoot() {
  Serial.println("Serving HTML page...");
  server.send_P(200, "text/html", htmlPage);
}

// API для получения списка устройств - ИСПРАВЛЕННАЯ ВЕРСИЯ
void handleApiDevices() {
  Serial.println("API devices requested");
  
  DynamicJsonDocument doc(2048);
  
  // Подсчет статистики
  int onlineCount = 0;
  int vrCount = 0;
  int fixedCount = 0;
  
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].connected) {
      onlineCount++;
    }
    if (devices[i].hasMPU6050) {
      vrCount++;
    }
    if (devices[i].ipFixed) {
      fixedCount++;
    }
  }
  
  doc["totalDevices"] = deviceCount;
  doc["onlineDevices"] = onlineCount;
  doc["vrDevices"] = vrCount;
  doc["fixedIPs"] = fixedCount;
  doc["espIp"] = WiFi.softAPIP().toString();
  
  JsonArray devicesArray = doc.createNestedArray("devices");
  
  for (int i = 0; i < deviceCount; i++) {
    JsonObject deviceObj = devicesArray.createNestedObject();
    deviceObj["ip"] = devices[i].ip;
    deviceObj["mac"] = devices[i].mac;
    deviceObj["originalMac"] = devices[i].originalMac;
    deviceObj["hostname"] = devices[i].hostname;
    deviceObj["displayName"] = getDisplayName(devices[i].mac, devices[i].hostname);
    deviceObj["rssi"] = devices[i].rssi;
    deviceObj["ipFixed"] = devices[i].ipFixed;
    deviceObj["hasMPU6050"] = devices[i].hasMPU6050;
    deviceObj["connected"] = devices[i].connected;
    
    if (devices[i].hasMPU6050) {
      deviceObj["pitch"] = devices[i].pitch;
      deviceObj["roll"] = devices[i].roll;
      deviceObj["yaw"] = devices[i].yaw;
      deviceObj["relPitch"] = devices[i].relPitch;
      deviceObj["relRoll"] = devices[i].relRoll;
      deviceObj["relYaw"] = devices[i].relYaw;
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
  Serial.println("API response sent successfully");
}

// API для калибровки датчика
void handleApiCalibrate() {
  if (server.method() == HTTP_POST) {
    String ip = server.arg("ip");
    Serial.printf("Calibration request for device: %s\n", ip.c_str());
    
    // Находим устройство по IP
    int deviceIndex = findDeviceByIP(ip.c_str());
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
    int deviceIndex = findDeviceByIP(ip.c_str());
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

// API для переименования устройства
void handleApiRename() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String name = server.arg("name");
    
    Serial.printf("Rename request - MAC: %s, Name: %s\n", mac.c_str(), name.c_str());
    
    if (mac.length() > 0 && name.length() > 0 && name.length() <= 30) {
      // Ищем существующий алиас
      int aliasIndex = findAliasByMAC(mac.c_str());
      
      if (aliasIndex != -1) {
        // Обновляем существующий алиас
        safeStrcpy(deviceAliases[aliasIndex].alias, name.c_str(), sizeof(deviceAliases[aliasIndex].alias));
        Serial.printf("Updated alias for MAC %s: %s\n", mac.c_str(), name.c_str());
      } else {
        // Создаем новый алиас
        if (aliasCount < MAX_ALIASES) {
          safeStrcpy(deviceAliases[aliasCount].mac, mac.c_str(), sizeof(deviceAliases[aliasCount].mac));
          safeStrcpy(deviceAliases[aliasCount].alias, name.c_str(), sizeof(deviceAliases[aliasCount].alias));
          aliasCount++;
          Serial.printf("Created new alias for MAC %s: %s\n", mac.c_str(), name.c_str());
        } else {
          server.send(507, "application/json", "{\"status\":\"error\",\"message\":\"Alias limit reached\"}");
          return;
        }
      }
      
      // Помечаем EEPROM как требующее сохранения
      eepromDirty = true;
      lastEEPROMSave = millis();
      
      server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Device name saved\"}");
      
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для фиксации IP адреса
void handleApiFixIP() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String ip = server.arg("ip");
    
    Serial.printf("Fix IP request - MAC: %s, IP: %s\n", mac.c_str(), ip.c_str());
    
    if (mac.length() > 0 && ip.length() > 0) {
      int ipNum = ip.toInt();
      if (ipNum < 2 || ipNum > 254) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"IP must be between 2 and 254\"}");
        return;
      }
      
      // Формируем полный IP адрес
      int subnet = atoi(networkSettings.subnet);
      char fullIP[16];
      snprintf(fullIP, sizeof(fullIP), "192.168.%d.%s", subnet, ip.c_str());
      
      // Ищем существующую запись
      int fixedIndex = findFixedIPByMAC(mac.c_str());
      
      if (fixedIndex != -1) {
        // Обновляем существующую запись
        safeStrcpy(fixedIPs[fixedIndex].ip, fullIP, sizeof(fixedIPs[fixedIndex].ip));
        Serial.printf("Updated fixed IP for MAC %s: %s\n", mac.c_str(), fullIP);
      } else {
        // Создаем новую запись
        if (fixedIPCount < MAX_FIXED_IPS) {
          safeStrcpy(fixedIPs[fixedIPCount].mac, mac.c_str(), sizeof(fixedIPs[fixedIPCount].mac));
          safeStrcpy(fixedIPs[fixedIPCount].ip, fullIP, sizeof(fixedIPs[fixedIPCount].ip));
          fixedIPCount++;
          Serial.printf("Created new fixed IP for MAC %s: %s\n", mac.c_str(), fullIP);
        } else {
          server.send(507, "application/json", "{\"status\":\"error\",\"message\":\"Fixed IP limit reached\"}");
          return;
        }
      }
      
      // Обновляем статус фиксации в устройствах
      int deviceIndex = findDeviceByMAC(mac.c_str());
      if (deviceIndex != -1) {
        devices[deviceIndex].ipFixed = true;
      }
      
      // Помечаем EEPROM как требующее сохранения
      eepromDirty = true;
      lastEEPROMSave = millis();
      
      server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"IP address fixed. Device will get this IP on next connection.\"}");
      
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для снятия фиксации IP адреса
void handleApiUnfixIP() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    
    Serial.printf("Unfix IP request - MAC: %s\n", mac.c_str());
    
    if (mac.length() > 0) {
      // Ищем запись
      int fixedIndex = findFixedIPByMAC(mac.c_str());
      
      if (fixedIndex != -1) {
        // Удаляем запись (сдвигаем массив)
        for (int i = fixedIndex; i < fixedIPCount - 1; i++) {
          memcpy(&fixedIPs[i], &fixedIPs[i + 1], sizeof(FixedIP));
        }
        fixedIPCount--;
        Serial.printf("Removed fixed IP for MAC %s\n", mac.c_str());
        
        // Обновляем статус фиксации в устройствах
        int deviceIndex = findDeviceByMAC(mac.c_str());
        if (deviceIndex != -1) {
          devices[deviceIndex].ipFixed = false;
        }
        
        // Помечаем EEPROM как требующее сохранения
        eepromDirty = true;
        lastEEPROMSave = millis();
        
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"IP address unfixed\"}");
      } else {
        server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"Fixed IP not found\"}");
      }
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters\"}");
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
      safeStrcpy(networkSettings.ssid, ssid.c_str(), sizeof(networkSettings.ssid));
      safeStrcpy(networkSettings.password, password.c_str(), sizeof(networkSettings.password));
      safeStrcpy(networkSettings.subnet, subnet.c_str(), sizeof(networkSettings.subnet));
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
  
  // Загрузка алиасов устройств и фиксированных IP из EEPROM
  loadAliasesFromEEPROM();
  
  // Настройка WiFi
  setupWiFiAP();
  
  // Настройка маршрутов сервера
  server.on("/", handleRoot);
  server.on("/api/devices", handleApiDevices);
  server.on("/api/calibrate", HTTP_POST, handleApiCalibrate);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/rename", HTTP_POST, handleApiRename);
  server.on("/api/fixip", HTTP_POST, handleApiFixIP);
  server.on("/api/unfixip", HTTP_POST, handleApiUnfixIP);
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
  
  // Буферизованное сохранение в EEPROM
  if (eepromDirty && (millis() - lastEEPROMSave >= EEPROM_SAVE_INTERVAL)) {
    saveAliasesToEEPROM();
  }
  
  delay(100);
}
