#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// Настройки WiFi по умолчанию
const char* ap_ssid = "ESP8266_Network_Monitor";
const char* ap_password = "12345678";

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

// Структура для фиксированных IP адресов и имен
struct FixedDevice {
  String mac;
  String ip;
  String customName;
};

// Структура для настроек сети
struct NetworkSettings {
  String ssid;
  String password;
  String subnet;
  bool configured;
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 50;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Массив для фиксированных устройств
const int MAX_FIXED_DEVICES = 20;
FixedDevice fixedDevices[MAX_FIXED_DEVICES];
int fixedDeviceCount = 0;

// Настройки сети
NetworkSettings networkSettings;

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// Прототипы функций
void saveFixedDevicesToEEPROM();
void loadFixedDevicesFromEEPROM();
void saveNetworkSettingsToEEPROM();
void loadNetworkSettingsFromEEPROM();
void clearFixedDevices();
int findDeviceByMAC(const String& mac);
int findFixedDeviceByMAC(const String& mac);
String getDisplayName(const String& mac, const String& originalHostname);
bool hasCustomName(const String& mac);
bool fixIPAddress(const String& mac, const String& ip);
bool setDeviceName(const String& mac, const String& name);
void setupWiFiAP();
void scanNetwork();

// Упрощенная HTML страница
const String htmlPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Network Monitor</title>
    <style>
        body { font-family: Arial; margin: 10px; background: #f0f0f0; }
        .container { max-width: 1000px; margin: 0 auto; background: white; padding: 15px; border-radius: 8px; }
        .header { text-align: center; margin-bottom: 15px; padding: 15px; background: #2c3e50; color: white; border-radius: 6px; }
        .stats { display: flex; justify-content: space-around; margin-bottom: 15px; flex-wrap: wrap; }
        .stat-card { background: #ecf0f1; padding: 8px; border-radius: 4px; text-align: center; min-width: 100px; margin: 3px; }
        .stat-number { font-size: 18px; font-weight: bold; color: #2c3e50; }
        .stat-label { color: #7f8c8d; font-size: 11px; }
        .devices-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); gap: 8px; margin-bottom: 15px; }
        .device-card { background: white; border: 1px solid #ddd; border-radius: 4px; padding: 8px; }
        .device-header { display: flex; justify-content: space-between; margin-bottom: 6px; }
        .device-name { font-weight: bold; color: #2c3e50; font-size: 14px; }
        .device-status { background: #27ae60; color: white; padding: 2px 5px; border-radius: 6px; font-size: 9px; }
        .status-fixed { background: #e67e22; }
        .info-row { display: flex; justify-content: space-between; margin-bottom: 2px; font-size: 11px; }
        .info-label { color: #7f8c8d; }
        .info-value { color: #2c3e50; font-family: monospace; }
        .controls { text-align: center; margin: 10px 0; }
        .btn { padding: 5px 10px; margin: 2px; border: none; border-radius: 3px; background: #3498db; color: white; cursor: pointer; font-size: 10px; }
        .btn:hover { background: #2980b9; }
        .btn-fixed { background: #9b59b6; }
        .btn-edit { background: #f39c12; }
        .btn-settings { background: #34495e; }
        .last-update { text-align: center; color: #7f8c8d; font-size: 11px; margin-top: 10px; }
        .no-devices { text-align: center; color: #7f8c8d; padding: 15px; }
        .fixed-badge { background: #9b59b6; color: white; padding: 1px 3px; border-radius: 4px; font-size: 8px; margin-left: 2px; }
        .name-badge { background: #f39c12; color: white; padding: 1px 3px; border-radius: 4px; font-size: 8px; margin-left: 2px; }
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
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1 style="margin:0;font-size:20px;">🖥️ Network Monitor</h1>
            <p style="margin:5px 0 0 0;font-size:12px;">Real-time device monitoring</p>
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
        
        <h3 style="margin:0 0 10px 0;font-size:16px;">📱 Connected Devices</h3>
        <div id="devicesContainer" class="devices-grid">
            <div class="no-devices">No devices found</div>
        </div>
        
        <div class="controls">
            <button class="btn" onclick="refreshData()" id="refreshBtn">🔄 Refresh</button>
            <button class="btn" onclick="startAutoRefresh()" id="autoRefreshBtn">🔄 Auto (10s)</button>
            <button class="btn btn-settings" onclick="showSettings()">⚙️ Settings</button>
        </div>
        
        <div class="last-update">
            Last updated: <span id="updateTime">Never</span>
        </div>
    </div>

    <div id="editModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <div class="modal-title">✏️ Edit Device Name</div>
                <span class="close" onclick="closeModal('editModal')">&times;</span>
            </div>
            <div class="form-group">
                <label class="form-label">MAC Address:</label>
                <input type="text" id="editMac" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">IP Address:</label>
                <input type="text" id="editIp" class="form-input" readonly>
            </div>
            <div class="form-group">
                <label class="form-label">Device Name:</label>
                <input type="text" id="editName" class="form-input" maxlength="20" placeholder="Enter custom name">
            </div>
            <div class="modal-buttons">
                <button class="btn" onclick="closeModal('editModal')">Cancel</button>
                <button class="btn btn-edit" onclick="saveDeviceName()">💾 Save</button>
            </div>
        </div>
    </div>

    <div id="settingsModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <div class="modal-title">⚙️ Network Settings</div>
                <span class="close" onclick="closeModal('settingsModal')">&times;</span>
            </div>
            <div class="warning">
                ⚠️ Changing subnet will clear all fixed IP addresses!
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
        let currentEditMac = '';
        
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
                            ${getDeviceIcon(device)} ${device.displayName}
                            ${device.ipFixed ? '<span class="fixed-badge">Fixed IP</span>' : ''}
                            ${device.hasCustomName ? '<span class="name-badge">Custom</span>' : ''}
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
                    <div style="text-align: center; margin-top: 6px; display: flex; justify-content: center; gap: 4px;">
                        <button class="btn btn-fixed" onclick="fixIP('${device.mac}', '${device.ip}')">📌 Fix IP</button>
                        <button class="btn btn-edit" onclick="editDeviceName('${device.mac}', '${device.ip}', '${device.displayName.replace(/'/g, "\\'")}')">✏️ Edit</button>
                    </div>
                </div>
            `).join('');
        }
        
        function getDeviceIcon(device) {
            const mac = (device.mac || '').toLowerCase();
            const name = (device.displayName || '').toLowerCase();
            if (mac.includes('apple') || name.includes('iphone') || name.includes('ipad')) return '📱';
            if (name.includes('android')) return '📱';
            if (name.includes('pc') || name.includes('desktop') || name.includes('laptop')) return '💻';
            if (mac.startsWith('b8:27:eb') || name.includes('raspberry')) return '🍓';
            return '🔌';
        }
        
        function formatMac(mac) {
            return mac ? mac.toUpperCase().match(/.{1,2}/g).join(':') : '';
        }
        
        function fixIP(mac, ip) {
            if (confirm(`Fix IP ${ip} for MAC ${formatMac(mac)}?`)) {
                const formData = new FormData();
                formData.append('mac', mac);
                formData.append('ip', ip);
                
                fetch('/api/fixip', {
                    method: 'POST',
                    body: formData
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
        
        function editDeviceName(mac, ip, currentName) {
            currentEditMac = mac;
            document.getElementById('editMac').value = formatMac(mac);
            document.getElementById('editIp').value = ip;
            document.getElementById('editName').value = currentName;
            document.getElementById('editModal').style.display = 'block';
        }
        
        function showSettings() {
            document.getElementById('settingsModal').style.display = 'block';
        }
        
        function closeModal(modalId) {
            document.getElementById(modalId).style.display = 'none';
            if (modalId === 'editModal') {
                currentEditMac = '';
            }
        }
        
        function saveDeviceName() {
            const newName = document.getElementById('editName').value.trim();
            const mac = currentEditMac;
            
            if (!newName) {
                alert('Please enter a device name');
                return;
            }
            
            const formData = new FormData();
            formData.append('mac', mac);
            formData.append('name', newName);
            
            fetch('/api/setname', {
                method: 'POST',
                body: formData
            })
            .then(r => r.json())
            .then(data => {
                if (data.status === 'success') {
                    closeModal('editModal');
                    refreshData();
                } else {
                    alert('Error: ' + (data.message || 'Unknown error'));
                }
            })
            .catch(e => {
                console.error('Error:', e);
                alert('Error saving device name');
            });
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
            
            if (!confirm('Changing subnet will clear all fixed IP addresses! Continue?')) {
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
                btn.innerHTML = '🔄 Auto (10s)';
            } else {
                autoRefreshInterval = setInterval(refreshData, 10000);
                btn.innerHTML = '⏹️ Stop';
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

// Функция для поиска фиксированного устройства по MAC
int findFixedDeviceByMAC(const String& mac) {
  for (int i = 0; i < fixedDeviceCount; i++) {
    if (fixedDevices[i].mac == mac) {
      return i;
    }
  }
  return -1;
}

// Функция для получения отображаемого имени устройства
String getDisplayName(const String& mac, const String& originalHostname) {
  int fixedIndex = findFixedDeviceByMAC(mac);
  if (fixedIndex != -1 && fixedDevices[fixedIndex].customName.length() > 0) {
    return fixedDevices[fixedIndex].customName;
  }
  return originalHostname.length() > 0 ? originalHostname : "Unknown";
}

// Функция для проверки наличия кастомного имени
bool hasCustomName(const String& mac) {
  int fixedIndex = findFixedDeviceByMAC(mac);
  return (fixedIndex != -1 && fixedDevices[fixedIndex].customName.length() > 0);
}

// Функция для добавления нового устройства
void addDevice(const String& ip, const String& mac, const String& hostname, int rssi) {
  int index = findDeviceByMAC(mac);
  unsigned long currentTime = millis() / 1000;
  
  // Проверяем есть ли фиксированное устройство для этого MAC
  int fixedIndex = findFixedDeviceByMAC(mac);
  String actualIP = ip;
  bool isFixed = false;
  String displayName = getDisplayName(mac, hostname);
  
  if (fixedIndex != -1 && fixedDevices[fixedIndex].ip.length() > 0) {
    actualIP = fixedDevices[fixedIndex].ip;
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
                   displayName.c_str(), actualIP.c_str(), mac.c_str(), rssi, isFixed ? "Yes" : "No");
    }
  } else {
    devices[index].lastSeen = currentTime;
    devices[index].rssi = rssi;
    devices[index].ip = actualIP;
    devices[index].ipFixed = isFixed;
    // Обновляем hostname только если нет кастомного имени
    if (hostname.length() > 0 && !hasCustomName(mac)) {
      devices[index].hostname = hostname;
    }
  }
}

// Функция для фиксации IP адреса
bool fixIPAddress(const String& mac, const String& ip) {
  int index = findFixedDeviceByMAC(mac);
  
  if (index == -1) {
    if (fixedDeviceCount < MAX_FIXED_DEVICES) {
      fixedDevices[fixedDeviceCount].mac = mac;
      fixedDevices[fixedDeviceCount].ip = ip;
      fixedDevices[fixedDeviceCount].customName = "";
      fixedDeviceCount++;
      
      // Обновляем устройство если оно есть в списке
      int deviceIndex = findDeviceByMAC(mac);
      if (deviceIndex != -1) {
        devices[deviceIndex].ip = ip;
        devices[deviceIndex].ipFixed = true;
      }
      
      saveFixedDevicesToEEPROM();
      return true;
    }
  } else {
    fixedDevices[index].ip = ip;
    
    // Обновляем устройство если оно есть в списке
    int deviceIndex = findDeviceByMAC(mac);
    if (deviceIndex != -1) {
      devices[deviceIndex].ip = ip;
      devices[deviceIndex].ipFixed = true;
    }
    
    saveFixedDevicesToEEPROM();
    return true;
  }
  return false;
}

// Функция для установки имени устройства
bool setDeviceName(const String& mac, const String& name) {
  int index = findFixedDeviceByMAC(mac);
  
  if (index == -1) {
    // Создаем новую запись с именем но без фиксированного IP
    if (fixedDeviceCount < MAX_FIXED_DEVICES) {
      fixedDevices[fixedDeviceCount].mac = mac;
      fixedDevices[fixedDeviceCount].ip = "";
      fixedDevices[fixedDeviceCount].customName = name;
      fixedDeviceCount++;
      
      Serial.printf("Custom name set: %s -> %s\n", mac.c_str(), name.c_str());
      saveFixedDevicesToEEPROM();
      return true;
    }
  } else {
    // Обновляем существующую запись
    fixedDevices[index].customName = name;
    Serial.printf("Custom name updated: %s -> %s\n", mac.c_str(), name.c_str());
    saveFixedDevicesToEEPROM();
    return true;
  }
  return false;
}

// Функция для очистки фиксированных устройств
void clearFixedDevices() {
  fixedDeviceCount = 0;
  Serial.println("Fixed devices cleared");
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

// Функция для сохранения фиксированных устройств в EEPROM
void saveFixedDevicesToEEPROM() {
  EEPROM.begin(512);
  
  int addr = 256; // Начинаем с адреса 256 для фиксированных устройств
  EEPROM.write(addr++, fixedDeviceCount);
  
  for (int i = 0; i < fixedDeviceCount; i++) {
    // Сохраняем MAC
    String mac = fixedDevices[i].mac;
    EEPROM.write(addr++, mac.length());
    for (int j = 0; j < mac.length(); j++) {
      EEPROM.write(addr++, mac[j]);
    }
    
    // Сохраняем IP
    String ip = fixedDevices[i].ip;
    EEPROM.write(addr++, ip.length());
    for (int j = 0; j < ip.length(); j++) {
      EEPROM.write(addr++, ip[j]);
    }
    
    // Сохраняем кастомное имя
    String name = fixedDevices[i].customName;
    EEPROM.write(addr++, name.length());
    for (int j = 0; j < name.length(); j++) {
      EEPROM.write(addr++, name[j]);
    }
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  Serial.printf("Fixed devices saved to EEPROM: %d\n", fixedDeviceCount);
}

// Функция для загрузки фиксированных устройств из EEPROM
void loadFixedDevicesFromEEPROM() {
  EEPROM.begin(512);
  
  int addr = 256;
  fixedDeviceCount = EEPROM.read(addr++);
  
  if (fixedDeviceCount > MAX_FIXED_DEVICES) fixedDeviceCount = 0;
  
  for (int i = 0; i < fixedDeviceCount; i++) {
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
    
    // Загружаем кастомное имя
    int nameLen = EEPROM.read(addr++);
    String name = "";
    for (int j = 0; j < nameLen; j++) {
      name += char(EEPROM.read(addr++));
    }
    
    fixedDevices[i].mac = mac;
    fixedDevices[i].ip = ip;
    fixedDevices[i].customName = name;
  }
  
  EEPROM.end();
  
  Serial.printf("Fixed devices loaded from EEPROM: %d\n", fixedDeviceCount);
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
    networkSettings.subnet = "4";
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

// Настройка WiFi в режиме точки доступа
void setupWiFiAP() {
  Serial.println("Setting up Access Point...");
  
  WiFi.mode(WIFI_AP);
  
  // Конвертируем подсеть в число
  int subnet = networkSettings.subnet.toInt();
  if (subnet < 1 || subnet > 254) {
    subnet = 4; // Значение по умолчанию при ошибке
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
  server.send(200, "text/html; charset=UTF-8", htmlPage);
}

// API для получения списка устройств
void handleApiDevices() {
  DynamicJsonDocument doc(2048);
  doc["totalDevices"] = deviceCount;
  doc["onlineDevices"] = deviceCount;
  doc["espIp"] = WiFi.softAPIP().toString();
  
  JsonArray devicesArray = doc.createNestedArray("devices");
  
  for (int i = 0; i < deviceCount; i++) {
    JsonObject deviceObj = devicesArray.createNestedObject();
    deviceObj["ip"] = devices[i].ip;
    deviceObj["mac"] = devices[i].mac;
    deviceObj["hostname"] = devices[i].hostname;
    deviceObj["displayName"] = getDisplayName(devices[i].mac, devices[i].hostname);
    deviceObj["rssi"] = devices[i].rssi;
    deviceObj["ipFixed"] = devices[i].ipFixed;
    deviceObj["hasCustomName"] = hasCustomName(devices[i].mac);
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json; charset=UTF-8", json);
}

// API для фиксации IP адреса
void handleApiFixIP() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String ip = server.arg("ip");
    
    Serial.printf("Fix IP request - MAC: %s, IP: %s\n", mac.c_str(), ip.c_str());
    
    if (mac.length() > 0 && ip.length() > 0) {
      if (fixIPAddress(mac, ip)) {
        server.send(200, "application/json", "{\"status\":\"success\"}");
        Serial.println("IP fixed successfully");
      } else {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Cannot fix IP - memory full\"}");
        Serial.println("Error: Cannot fix IP - memory full");
      }
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters - MAC or IP empty\"}");
      Serial.println("Error: Missing parameters");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
    Serial.println("Error: Method not allowed");
  }
}

// API для установки имени устройства
void handleApiSetName() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String name = server.arg("name");
    
    Serial.printf("Set name request - MAC: %s, Name: %s\n", mac.c_str(), name.c_str());
    
    if (mac.length() > 0 && name.length() > 0) {
      if (setDeviceName(mac, name)) {
        server.send(200, "application/json", "{\"status\":\"success\"}");
        Serial.println("Device name set successfully");
      } else {
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Cannot set name - memory full\"}");
        Serial.println("Error: Cannot set name - memory full");
      }
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters - MAC or name empty\"}");
      Serial.println("Error: Missing parameters");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
    Serial.println("Error: Method not allowed");
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
      // Очищаем фиксированные устройства при смене подсети
      if (networkSettings.subnet != subnet) {
        clearFixedDevices();
        saveFixedDevicesToEEPROM();
        Serial.println("Fixed IPs cleared due to subnet change");
      }
      
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
  
  Serial.println("\nStarting ESP8266 Network Monitor...");
  
  // Очистка EEPROM при первом запуске (раскомментировать для очистки)
  // clearEEPROM();
  
  // Загрузка настроек сети из EEPROM
  loadNetworkSettingsFromEEPROM();
  
  // Загрузка фиксированных устройств из EEPROM
  loadFixedDevicesFromEEPROM();
  
  // Настройка WiFi
  setupWiFiAP();
  
  // Настройка маршрутов сервера
  server.on("/", handleRoot);
  server.on("/api/devices", handleApiDevices);
  server.on("/api/fixip", HTTP_POST, handleApiFixIP);
  server.on("/api/setname", HTTP_POST, handleApiSetName);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  
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
