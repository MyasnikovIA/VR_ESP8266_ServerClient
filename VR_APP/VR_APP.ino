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

// DHCP сервер
#include <WiFiUdp.h>
WiFiUDP udp;
const unsigned int DHCP_SERVER_PORT = 67;
const unsigned int DHCP_CLIENT_PORT = 68;

// DHCP структуры
struct DHCPMessage {
    uint8_t op;      // Message op code / message type.
    uint8_t htype;   // Hardware address type
    uint8_t hlen;    // Hardware address length
    uint8_t hops;    // Hops
    uint32_t xid;    // Transaction ID
    uint16_t secs;   // Seconds
    uint16_t flags;  // Flags
    uint32_t ciaddr; // Client IP address
    uint32_t yiaddr; // Your (client) IP address
    uint32_t siaddr; // Next server IP address
    uint32_t giaddr; // Relay agent IP address
    uint8_t chaddr[16]; // Client hardware address
    uint8_t sname[64];  // Server host name
    uint8_t file[128];  // Boot file name
    uint8_t options[312]; // Optional parameters
};

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
  unsigned long lastSeen;
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

// DHCP пул адресов
IPAddress dhcpStartIP;
IPAddress dhcpEndIP;
const int DHCP_POOL_SIZE = 50;
bool dhcpLeases[DHCP_POOL_SIZE];
String dhcpMacTable[DHCP_POOL_SIZE];

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// Время последней записи в EEPROM
unsigned long lastEEPROMSave = 0;
const unsigned long EEPROM_SAVE_INTERVAL = 5000;
bool eepromDirty = false;

// Время последней проверки восстановления
unsigned long lastRecoveryCheck = 0;
const unsigned long RECOVERY_CHECK_INTERVAL = 30000;

// Флаги сбоев
bool wifiFailure = false;
bool memoryFailure = false;
unsigned long lastRestartAttempt = 0;
const unsigned long RESTART_INTERVAL = 60000;

// Добавьте эту строку в раздел объявлений функций (примерно после строки 80):
bool parseMPU6050Data(const String& message, char* deviceName, 
                     float& pitch, float& roll, float& yaw,
                     float& relPitch, float& relRoll, float& relYaw);
                     
// Вспомогательная функция для извлечения значения из строки с защитой от переполнения
float extractValue(const String& message, const String& key) {
  if (message.length() == 0 || key.length() == 0) return 0.0;
  
  int keyPos = message.indexOf(key);
  if (keyPos == -1) return 0.0;
  
  int valueStart = keyPos + key.length();
  if (valueStart >= message.length()) return 0.0;
  
  int valueEnd = message.indexOf(',', valueStart);
  if (valueEnd == -1) valueEnd = message.length();
  if (valueEnd > valueStart + 15) valueEnd = valueStart + 15; // Ограничение длины
  
  String valueStr = message.substring(valueStart, valueEnd);
  return valueStr.toFloat();
}

// Безопасное копирование строк с проверкой границ
void safeStrcpy(char* dest, const char* src, size_t destSize) {
  if (dest == NULL || src == NULL || destSize == 0) return;
  
  size_t srcLen = strlen(src);
  if (srcLen >= destSize) {
    srcLen = destSize - 1;
  }
  
  strncpy(dest, src, srcLen);
  dest[srcLen] = '\0';
}

// Функция для поиска устройства в массиве
int findDeviceByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < deviceCount; i++) {
    if (strcmp(devices[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска устройства по IP
int findDeviceByIP(const char* ip) {
  if (ip == NULL) return -1;
  
  for (int i = 0; i < deviceCount; i++) {
    if (strcmp(devices[i].ip, ip) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска алиаса по MAC
int findAliasByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < aliasCount; i++) {
    if (strcmp(deviceAliases[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска фиксированного IP по MAC
int findFixedIPByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < fixedIPCount; i++) {
    if (strcmp(fixedIPs[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для получения отображаемого имени устройства
String getDisplayName(const char* mac, const char* originalHostname) {
  if (mac == NULL) return "Unknown";
  
  // Сначала ищем пользовательское имя
  int aliasIndex = findAliasByMAC(mac);
  if (aliasIndex != -1) {
    return String(deviceAliases[aliasIndex].alias);
  }
  
  // Если пользовательского имени нет, используем оригинальное
  return originalHostname && strlen(originalHostname) > 0 ? String(originalHostname) : "Unknown";
}

// Функция для добавления нового устройства с защитой от переполнения
bool addDevice(const char* ip, const char* mac, const char* hostname, int rssi) {
  if (ip == NULL || mac == NULL) return false;
  
  // Проверяем длину входных данных
  if (strlen(ip) >= 16 || strlen(mac) >= 18 || (hostname && strlen(hostname) >= 32)) {
    Serial.println("Error: Input data too long for device structure");
    return false;
  }
  
  int index = findDeviceByMAC(mac);
  
  if (index == -1) {
    if (deviceCount < MAX_DEVICES) {
      safeStrcpy(devices[deviceCount].ip, ip, sizeof(devices[deviceCount].ip));
      safeStrcpy(devices[deviceCount].mac, mac, sizeof(devices[deviceCount].mac));
      safeStrcpy(devices[deviceCount].originalMac, mac, sizeof(devices[deviceCount].originalMac));
      
      if (hostname && strlen(hostname) > 0) {
        safeStrcpy(devices[deviceCount].hostname, hostname, sizeof(devices[deviceCount].hostname));
      } else {
        safeStrcpy(devices[deviceCount].hostname, "Unknown", sizeof(devices[deviceCount].hostname));
      }
      
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
      devices[deviceCount].lastSeen = millis();
      deviceCount++;
      
      Serial.printf("New device: %s (%s) - %s - RSSI: %d - IP Fixed: %s\n", 
                   hostname ? hostname : "Unknown", ip, mac, rssi, devices[deviceCount-1].ipFixed ? "YES" : "NO");
      return true;
    } else {
      Serial.println("Device limit reached!");
      return false;
    }
  } else {
    devices[index].rssi = rssi;
    safeStrcpy(devices[index].ip, ip, sizeof(devices[index].ip));
    devices[index].connected = true;
    devices[index].lastSeen = millis();
    if (hostname && strlen(hostname) > 0) {
      safeStrcpy(devices[index].hostname, hostname, sizeof(devices[index].hostname));
    }
    return true;
  }
}

// Функция для обновления данных MPU6050 от VR-клиента
void updateVRDeviceData(const char* ip, const char* deviceName, 
                       float pitch, float roll, float yaw,
                       float relPitch, float relRoll, float relYaw) {
  if (ip == NULL || deviceName == NULL) return;
  
  int index = findDeviceByIP(ip);
  
  if (index == -1) {
    // Создаем новое устройство если не найдено
    if (deviceCount < MAX_DEVICES) {
      safeStrcpy(devices[deviceCount].ip, ip, sizeof(devices[deviceCount].ip));
      
      char vrMac[32];
      snprintf(vrMac, sizeof(vrMac), "VR:%s", deviceName);
      // safeStrcpy(devices[deviceCount].mac, vrMac, sizeof(devices[deviceCount].mac));
      // safeStrcpy(devices[deviceCount].originalMac, vrMac, sizeof(devices[deviceCount].originalMac));
      
      safeStrcpy(devices[deviceCount].hostname, deviceName, sizeof(devices[deviceCount].hostname));
      safeStrcpy(devices[deviceCount].customName, "", sizeof(devices[deviceCount].customName));
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
      devices[deviceCount].lastSeen = millis();
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
    devices[index].lastSeen = millis();
    
    if (deviceName && strlen(deviceName) > 0) {
      safeStrcpy(devices[index].hostname, deviceName, sizeof(devices[index].hostname));
    }
    
    // Отправляем обновление через WebSocket всем веб-клиентам
    DynamicJsonDocument doc(512);
    if (doc.capacity() == 0) {
      Serial.println("Error: Failed to allocate JSON document");
      return;
    }
    
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
    if (serializeJson(doc, json) == 0) {
      Serial.println("Error: Failed to serialize JSON");
    } else {
      webSocketWeb.broadcastTXT(json);
    }
    
    Serial.printf("VR data updated for %s: Pitch=%.1f, Roll=%.1f, Yaw=%.1f\n", ip, pitch, roll, yaw);
  }
}

// Функция сканирования сети
// Функция сканирования сети
void scanNetwork() {
  Serial.println("Starting network scan...");
  
  // Помечаем все устройства как отключенные
  for (int i = 0; i < deviceCount; i++) {
    devices[i].connected = false;
  }
  
  // Проверяем активные подключения к точке доступа
  struct station_info *station = wifi_softap_get_station_info();
  while (station != NULL) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             station->bssid[0], station->bssid[1], station->bssid[2],
             station->bssid[3], station->bssid[4], station->bssid[5]);
    
    char ip[16];
    IPAddress ipAddr = IPAddress(station->ip);
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
    
    // Проверяем, является ли устройство VR-устройством
    bool isVRDevice = false;
    for (int i = 0; i < deviceCount; i++) {
      if (strcmp(devices[i].mac, mac) == 0 && devices[i].hasMPU6050) {
        isVRDevice = true;
        break;
      }
    }
    
    // Добавляем устройство только если:
    // 1. Это VR-устройство, ИЛИ
    // 2. Это не дубликат (по MAC-адресу)
    if (isVRDevice || findDeviceByMAC(mac) == -1) {
      // Используем фиксированное значение RSSI, так как структура не содержит эту информацию
      addDevice(ip, mac, "WiFi Client", -50);
    } else {
      Serial.printf("Skipping duplicate device: %s (%s)\n", mac, ip);
    }
    
    station = STAILQ_NEXT(station, next);
  }
  wifi_softap_free_station_info();
  
  // Помечаем VR-устройства как онлайн если они обновлялись недавно
  unsigned long currentTime = millis();
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].hasMPU6050 && (currentTime - devices[i].lastUpdate) < 30000) {
      devices[i].connected = true;
    }
    
    // Помечаем устройства как отключенные если не видели их больше минуты
    if ((currentTime - devices[i].lastSeen) > 60000) {
      devices[i].connected = false;
    }
  }
  
  Serial.printf("Scan complete. Found %d devices\n", deviceCount);
  lastScanTime = millis();
}

// Инициализация DHCP сервера
void initDHCPServer() {
  int subnet = atoi(networkSettings.subnet);
  if (subnet < 1 || subnet > 254) {
    subnet = 50;
  }
  
  dhcpStartIP = IPAddress(192, 168, subnet, 2);
  dhcpEndIP = IPAddress(192, 168, subnet, 50);
  
  // Инициализация пула адресов
  for (int i = 0; i < DHCP_POOL_SIZE; i++) {
    dhcpLeases[i] = false;
    dhcpMacTable[i] = "";
  }
  
  // Запуск UDP сервера для DHCP
  if (udp.begin(DHCP_SERVER_PORT)) {
    Serial.println("DHCP Server started on port 67");
  } else {
    Serial.println("Failed to start DHCP server");
  }
}

// Обработка DHCP запросов
void handleDHCP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    if (packetSize >= sizeof(DHCPMessage)) {
      Serial.println("DHCP packet too large");
      return;
    }
    
    DHCPMessage dhcpMsg;
    udp.read((uint8_t*)&dhcpMsg, sizeof(DHCPMessage));
    
    // Определяем тип DHCP сообщения
    uint8_t messageType = 0;
    for (int i = 0; i < 312; i++) {
      if (dhcpMsg.options[i] == 53) { // DHCP Message Type
        messageType = dhcpMsg.options[i + 2];
        break;
      }
    }
    
    if (messageType == 1) { // DHCP Discover
      handleDHCPDiscover(dhcpMsg);
    } else if (messageType == 3) { // DHCP Request
      handleDHCPRequest(dhcpMsg);
    }
  }
}

// Обработка DHCP Discover
void handleDHCPDiscover(DHCPMessage& discoverMsg) {
  Serial.println("DHCP Discover received");
  
  // Ищем свободный IP в пуле
  int freeIPIndex = -1;
  String clientMAC = macToString(discoverMsg.chaddr);
  
  // Сначала проверяем, есть ли уже аренда для этого MAC
  for (int i = 0; i < DHCP_POOL_SIZE; i++) {
    if (dhcpMacTable[i] == clientMAC) {
      freeIPIndex = i;
      break;
    }
  }
  
  // Если нет, ищем свободный IP
  if (freeIPIndex == -1) {
    for (int i = 0; i < DHCP_POOL_SIZE; i++) {
      if (!dhcpLeases[i]) {
        freeIPIndex = i;
        break;
      }
    }
  }
  
  if (freeIPIndex != -1) {
    // Отправляем DHCP Offer
    sendDHCPOffer(discoverMsg, freeIPIndex);
  }
}

// Обработка DHCP Request
void handleDHCPRequest(DHCPMessage& requestMsg) {
  Serial.println("DHCP Request received");
  
  String clientMAC = macToString(requestMsg.chaddr);
  int subnet = atoi(networkSettings.subnet);
  
  // Отправляем DHCP Ack
  sendDHCPAck(requestMsg, clientMAC);
  
  // Добавляем устройство в список
  IPAddress clientIP = IPAddress(192, 168, subnet, getIPFromMAC(clientMAC));
  addDevice(clientIP.toString().c_str(), clientMAC.c_str(), "DHCP Client", -50);
}

// Отправка DHCP Offer
void sendDHCPOffer(DHCPMessage& discoverMsg, int ipIndex) {
  DHCPMessage offerMsg;
  memset(&offerMsg, 0, sizeof(DHCPMessage));
  
  offerMsg.op = 2; // BOOTREPLY
  offerMsg.htype = 1; // Ethernet
  offerMsg.hlen = 6;
  offerMsg.xid = discoverMsg.xid;
  
  int subnet = atoi(networkSettings.subnet);
  offerMsg.yiaddr = IPAddress(192, 168, subnet, ipIndex + 2).v4();
  offerMsg.siaddr = WiFi.softAPIP().v4();
  
  memcpy(offerMsg.chaddr, discoverMsg.chaddr, 16);
  strcpy((char*)offerMsg.sname, "ESP8266_DHCP");
  
  // Добавляем опции
  int optIndex = 0;
  offerMsg.options[optIndex++] = 53; // DHCP Message Type
  offerMsg.options[optIndex++] = 1;
  offerMsg.options[optIndex++] = 2; // Offer
  
  offerMsg.options[optIndex++] = 1; // Subnet Mask
  offerMsg.options[optIndex++] = 4;
  uint32_t subnetMask = IPAddress(255, 255, 255, 0).v4();
  memcpy(&offerMsg.options[optIndex], &subnetMask, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 3; // Router
  offerMsg.options[optIndex++] = 4;
  uint32_t router = WiFi.softAPIP().v4();
  memcpy(&offerMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 51; // IP Lease Time
  offerMsg.options[optIndex++] = 4;
  uint32_t leaseTime = 3600; // 1 hour
  memcpy(&offerMsg.options[optIndex], &leaseTime, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 54; // DHCP Server Identifier
  offerMsg.options[optIndex++] = 4;
  memcpy(&offerMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 255; // End option
  
  // Отправка пакета
  udp.beginPacket(IPAddress(255, 255, 255, 255), DHCP_CLIENT_PORT);
  udp.write((uint8_t*)&offerMsg, sizeof(DHCPMessage));
  udp.endPacket();
  
  // Резервируем IP
  dhcpLeases[ipIndex] = true;
  dhcpMacTable[ipIndex] = macToString(discoverMsg.chaddr);
  
  Serial.printf("DHCP Offer sent for IP: 192.168.%d.%d\n", subnet, ipIndex + 2);
}

// Отправка DHCP Ack
void sendDHCPAck(DHCPMessage& requestMsg, const String& clientMAC) {
  DHCPMessage ackMsg;
  memset(&ackMsg, 0, sizeof(DHCPMessage));
  
  ackMsg.op = 2; // BOOTREPLY
  ackMsg.htype = 1;
  ackMsg.hlen = 6;
  ackMsg.xid = requestMsg.xid;
  
  int subnet = atoi(networkSettings.subnet);
  int clientIP = getIPFromMAC(clientMAC);
  ackMsg.yiaddr = IPAddress(192, 168, subnet, clientIP).v4();
  ackMsg.siaddr = WiFi.softAPIP().v4();
  
  memcpy(ackMsg.chaddr, requestMsg.chaddr, 16);
  strcpy((char*)ackMsg.sname, "ESP8266_DHCP");
  
  // Добавляем опции
  int optIndex = 0;
  ackMsg.options[optIndex++] = 53; // DHCP Message Type
  ackMsg.options[optIndex++] = 1;
  ackMsg.options[optIndex++] = 5; // ACK
  
  ackMsg.options[optIndex++] = 1; // Subnet Mask
  ackMsg.options[optIndex++] = 4;
  uint32_t subnetMask = IPAddress(255, 255, 255, 0).v4();
  memcpy(&ackMsg.options[optIndex], &subnetMask, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 3; // Router
  ackMsg.options[optIndex++] = 4;
  uint32_t router = WiFi.softAPIP().v4();
  memcpy(&ackMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 51; // IP Lease Time
  ackMsg.options[optIndex++] = 4;
  uint32_t leaseTime = 3600;
  memcpy(&ackMsg.options[optIndex], &leaseTime, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 54; // DHCP Server Identifier
  ackMsg.options[optIndex++] = 4;
  memcpy(&ackMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 255; // End option
  
  udp.beginPacket(IPAddress(255, 255, 255, 255), DHCP_CLIENT_PORT);
  udp.write((uint8_t*)&ackMsg, sizeof(DHCPMessage));
  udp.endPacket();
  
  Serial.printf("DHCP ACK sent for IP: 192.168.%d.%d to MAC: %s\n", subnet, clientIP, clientMAC.c_str());
}

// Вспомогательная функция для преобразования MAC в строку
String macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// Получение IP из MAC
int getIPFromMAC(const String& mac) {
  for (int i = 0; i < DHCP_POOL_SIZE; i++) {
    if (dhcpMacTable[i] == mac) {
      return i + 2;
    }
  }
  return -1;
}

// Функция для сохранения настроек сети в EEPROM
void saveNetworkSettingsToEEPROM() {
  EEPROM.begin(2048);
  
  int addr = 0;
  EEPROM.write(addr++, networkSettings.configured ? 1 : 0);
  
  // Сохраняем SSID
  size_t ssidLen = strlen(networkSettings.ssid);
  if (ssidLen > 31) ssidLen = 31;
  EEPROM.write(addr++, ssidLen);
  for (size_t j = 0; j < ssidLen; j++) {
    EEPROM.write(addr++, networkSettings.ssid[j]);
  }
  
  // Сохраняем пароль
  size_t passwordLen = strlen(networkSettings.password);
  if (passwordLen > 31) passwordLen = 31;
  EEPROM.write(addr++, passwordLen);
  for (size_t j = 0; j < passwordLen; j++) {
    EEPROM.write(addr++, networkSettings.password[j]);
  }
  
  // Сохраняем подсеть
  size_t subnetLen = strlen(networkSettings.subnet);
  if (subnetLen > 3) subnetLen = 3;
  EEPROM.write(addr++, subnetLen);
  for (size_t j = 0; j < subnetLen; j++) {
    EEPROM.write(addr++, networkSettings.subnet[j]);
  }
  
  if (EEPROM.commit()) {
    Serial.println("Network settings saved to EEPROM");
  } else {
    Serial.println("Error saving network settings to EEPROM");
    memoryFailure = true;
  }
  EEPROM.end();
}

// Функция для загрузки настроек сети из EEPROM
void loadNetworkSettingsFromEEPROM() {
  EEPROM.begin(2048);
  
  int addr = 0;
  networkSettings.configured = (EEPROM.read(addr++) == 1);
  
  if (networkSettings.configured) {
    // Загружаем SSID
    int ssidLen = EEPROM.read(addr++);
    if (ssidLen > 31) ssidLen = 31;
    for (int j = 0; j < ssidLen; j++) {
      networkSettings.ssid[j] = EEPROM.read(addr++);
    }
    networkSettings.ssid[ssidLen] = '\0';
    
    // Загружаем пароль
    int passwordLen = EEPROM.read(addr++);
    if (passwordLen > 31) passwordLen = 31;
    for (int j = 0; j < passwordLen; j++) {
      networkSettings.password[j] = EEPROM.read(addr++);
    }
    networkSettings.password[passwordLen] = '\0';
    
    // Загружаем подсеть
    int subnetLen = EEPROM.read(addr++);
    if (subnetLen > 3) subnetLen = 3;
    for (int j = 0; j < subnetLen; j++) {
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
  
  int addr = 512;
  EEPROM.write(addr++, aliasCount > MAX_ALIASES ? MAX_ALIASES : aliasCount);
  
  for (int i = 0; i < aliasCount && i < MAX_ALIASES; i++) {
    // Сохраняем MAC
    size_t macLen = strlen(deviceAliases[i].mac);
    if (macLen > 17) macLen = 17;
    EEPROM.write(addr++, macLen);
    for (size_t j = 0; j < macLen; j++) {
      EEPROM.write(addr++, deviceAliases[i].mac[j]);
    }
    
    // Сохраняем алиас
    size_t aliasLen = strlen(deviceAliases[i].alias);
    if (aliasLen > 31) aliasLen = 31;
    EEPROM.write(addr++, aliasLen);
    for (size_t j = 0; j < aliasLen; j++) {
      EEPROM.write(addr++, deviceAliases[i].alias[j]);
    }
  }
  
  // Сохраняем фиксированные IP
  addr = 1024;
  EEPROM.write(addr++, fixedIPCount > MAX_FIXED_IPS ? MAX_FIXED_IPS : fixedIPCount);
  
  for (int i = 0; i < fixedIPCount && i < MAX_FIXED_IPS; i++) {
    // Сохраняем MAC
    size_t macLen = strlen(fixedIPs[i].mac);
    if (macLen > 17) macLen = 17;
    EEPROM.write(addr++, macLen);
    for (size_t j = 0; j < macLen; j++) {
      EEPROM.write(addr++, fixedIPs[i].mac[j]);
    }
    
    // Сохраняем IP
    size_t ipLen = strlen(fixedIPs[i].ip);
    if (ipLen > 15) ipLen = 15;
    EEPROM.write(addr++, ipLen);
    for (size_t j = 0; j < ipLen; j++) {
      EEPROM.write(addr++, fixedIPs[i].ip[j]);
    }
  }
  
  if (EEPROM.commit()) {
    Serial.println("Device aliases and fixed IPs saved to EEPROM");
    eepromDirty = false;
  } else {
    Serial.println("Error saving aliases to EEPROM");
    memoryFailure = true;
  }
  EEPROM.end();
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
    if (macLen > 17) macLen = 17;
    for (int j = 0; j < macLen; j++) {
      deviceAliases[i].mac[j] = EEPROM.read(addr++);
    }
    deviceAliases[i].mac[macLen] = '\0';
    
    // Загружаем алиас
    int aliasLen = EEPROM.read(addr++);
    if (aliasLen > 31) aliasLen = 31;
    for (int j = 0; j < aliasLen; j++) {
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
    if (macLen > 17) macLen = 17;
    for (int j = 0; j < macLen; j++) {
      fixedIPs[i].mac[j] = EEPROM.read(addr++);
    }
    fixedIPs[i].mac[macLen] = '\0';
    
    // Загружаем IP
    int ipLen = EEPROM.read(addr++);
    if (ipLen > 15) ipLen = 15;
    for (int j = 0; j < ipLen; j++) {
      fixedIPs[i].ip[j] = EEPROM.read(addr++);
    }
    fixedIPs[i].ip[ipLen] = '\0';
  }
  
  EEPROM.end();
  
  Serial.printf("Loaded %d aliases and %d fixed IPs from EEPROM\n", aliasCount, fixedIPCount);
}

// Функция восстановления при сбоях
void recoveryCheck() {
  unsigned long currentTime = millis();
  
  // Проверяем сбои WiFi
  if (wifiFailure) {
    Serial.println("WiFi failure detected, attempting recovery...");
    
    // Перезапускаем точку доступа
    WiFi.softAPdisconnect(true);
    delay(1000);
    
    int subnet = atoi(networkSettings.subnet);
    IPAddress local_ip(192, 168, subnet, 1);
    IPAddress gateway(192, 168, subnet, 1);
    IPAddress subnet_mask(255, 255, 255, 0);
    
    WiFi.softAPConfig(local_ip, gateway, subnet_mask);
    
    if (WiFi.softAP(networkSettings.ssid, networkSettings.password)) {
      Serial.println("WiFi AP restarted successfully");
      wifiFailure = false;
    } else {
      Serial.println("Failed to restart WiFi AP");
    }
  }
  
  // Проверяем сбои памяти
  if (memoryFailure) {
    Serial.println("Memory failure detected, attempting recovery...");
    
    // Перезапускаем EEPROM
    EEPROM.begin(2048);
    EEPROM.end();
    
    // Перезагружаем настройки
    loadNetworkSettingsFromEEPROM();
    loadAliasesFromEEPROM();
    
    memoryFailure = false;
    Serial.println("Memory recovery completed");
  }
  
  // Проверяем необходимость перезагрузки
  if (currentTime - lastRestartAttempt > RESTART_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0) {
      Serial.println("System appears unstable, attempting restart...");
      ESP.restart();
    }
    lastRestartAttempt = currentTime;
  }
  
  lastRecoveryCheck = currentTime;
}

// ВАЖНО: Восстанавливаем оригинальную HTML страницу
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

// Восстанавливаем оригинальные обработчики API
// API для получения списка устройств с объединением дубликатов по IP
void handleApiDevices() {
  Serial.println("API devices requested");
  
  DynamicJsonDocument doc(2048);
  
  // Временный массив для объединения устройств по IP
  struct MergedDevice {
    String ip;
    bool processed;
    int deviceIndex;
  };
  
  MergedDevice mergedDevices[MAX_DEVICES];
  int mergedCount = 0;
  
  // Сначала собираем все уникальные IP
  for (int i = 0; i < deviceCount; i++) {
    bool ipExists = false;
    for (int j = 0; j < mergedCount; j++) {
      if (mergedDevices[j].ip == devices[i].ip) {
        ipExists = true;
        break;
      }
    }
    
    if (!ipExists) {
      mergedDevices[mergedCount].ip = devices[i].ip;
      mergedDevices[mergedCount].processed = false;
      mergedDevices[mergedCount].deviceIndex = i;
      mergedCount++;
    }
  }
  
  // Теперь подсчитываем статистику на основе объединенных устройств
  int onlineCount = 0;
  int vrCount = 0;
  int fixedCount = 0;
  int actualDeviceCount = 0; // Реальное количество устройств после объединения
  
  JsonArray devicesArray = doc.createNestedArray("devices");
  
  // Обрабатываем каждый уникальный IP и считаем статистику
  for (int i = 0; i < mergedCount; i++) {
    if (mergedDevices[i].processed) continue;
    
    String currentIP = mergedDevices[i].ip;
    actualDeviceCount++; // Увеличиваем счетчик реальных устройств
    
    // Ищем все устройства с этим IP
    DeviceInfo* vrDevice = nullptr;
    DeviceInfo* normalDevice = nullptr;
    bool isOnline = false;
    bool isVR = false;
    bool isFixed = false;
    
    for (int j = 0; j < deviceCount; j++) {
      if (String(devices[j].ip) == currentIP) {
        if (devices[j].hasMPU6050) {
          vrDevice = &devices[j];
          isVR = true;
          if (devices[j].connected) isOnline = true;
        } else {
          normalDevice = &devices[j];
          if (devices[j].connected) isOnline = true;
          if (devices[j].ipFixed) isFixed = true;
        }
        mergedDevices[i].processed = true;
      }
    }
    
    // Обновляем статистику
    if (isOnline) onlineCount++;
    if (isVR) vrCount++;
    if (isFixed) fixedCount++;
    
    // Создаем объединенное устройство
    JsonObject deviceObj = devicesArray.createNestedObject();
    
    if (vrDevice != nullptr) {
      // Используем данные VR устройства как основу
      deviceObj["ip"] = vrDevice->ip;
      deviceObj["mac"] = vrDevice->mac;
      deviceObj["originalMac"] = vrDevice->originalMac;
      deviceObj["hostname"] = vrDevice->hostname;
      deviceObj["displayName"] = getDisplayName(vrDevice->mac, vrDevice->hostname);
      deviceObj["rssi"] = vrDevice->rssi;
      deviceObj["ipFixed"] = vrDevice->ipFixed;
      deviceObj["hasMPU6050"] = vrDevice->hasMPU6050;
      deviceObj["connected"] = vrDevice->connected;
      
      // Данные сенсора
      deviceObj["pitch"] = vrDevice->pitch;
      deviceObj["roll"] = vrDevice->roll;
      deviceObj["yaw"] = vrDevice->yaw;
      deviceObj["relPitch"] = vrDevice->relPitch;
      deviceObj["relRoll"] = vrDevice->relRoll;
      deviceObj["relYaw"] = vrDevice->relYaw;
      
    } else if (normalDevice != nullptr) {
      // Используем данные обычного устройства
      deviceObj["ip"] = normalDevice->ip;
      deviceObj["mac"] = normalDevice->mac;
      deviceObj["originalMac"] = normalDevice->originalMac;
      deviceObj["hostname"] = normalDevice->hostname;
      deviceObj["displayName"] = getDisplayName(normalDevice->mac, normalDevice->hostname);
      deviceObj["rssi"] = normalDevice->rssi;
      deviceObj["ipFixed"] = normalDevice->ipFixed;
      deviceObj["hasMPU6050"] = normalDevice->hasMPU6050;
      deviceObj["connected"] = normalDevice->connected;
    }
    
    // Если есть оба типа устройств, объединяем лучшие атрибуты
    if (vrDevice != nullptr && normalDevice != nullptr) {
      // Предпочитаем MAC адрес из нормального устройства (он настоящий)
      if (strlen(normalDevice->mac) > 0) {
        deviceObj["mac"] = normalDevice->mac;
        deviceObj["originalMac"] = normalDevice->originalMac;
      }
      
      // Предпочитаем настройки IP из нормального устройства
      deviceObj["ipFixed"] = normalDevice->ipFixed;
      
      // Состояние подключения берем из VR устройства (оно более актуально)
      deviceObj["connected"] = vrDevice->connected;
      
      Serial.printf("Merged devices for IP %s: VR + Normal\n", currentIP.c_str());
    }
  }
  
  // Используем ПЕРЕСЧИТАННЫЕ значения
  doc["totalDevices"] = actualDeviceCount;        // После объединения дубликатов
  doc["onlineDevices"] = onlineCount;             // Пересчитанные онлайн устройства
  doc["vrDevices"] = vrCount;                     // Пересчитанные VR устройства
  doc["fixedIPs"] = fixedCount;                   // Пересчитанные фиксированные IP
  doc["espIp"] = WiFi.softAPIP().toString();
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
  
  Serial.printf("API response: %d devices (after merging duplicates from %d original)\n", 
                actualDeviceCount, deviceCount);
  Serial.printf("Online: %d, VR: %d, Fixed IPs: %d\n", onlineCount, vrCount, fixedCount);
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

// Восстанавливаем оригинальный обработчик главной страницы
void handleRoot() {
  Serial.println("Serving HTML page...");
  server.send_P(200, "text/html", htmlPage);
}

// Восстанавливаем оригинальные WebSocket обработчики
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
        }
        else if (message == "ANGLES_RESET") {
          Serial.printf("[VR %u] Angles reset to zero\n", num);
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

// Восстанавливаем функцию парсинга данных MPU6050
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

// Восстанавливаем WebSocket обработчик для веб-клиентов
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

// Восстанавливаем оригинальную настройку WiFi
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\nStarting VR Tracking Server...");
  
  // Загрузка настроек сети из EEPROM
  loadNetworkSettingsFromEEPROM();
  
  // Загрузка алиасов устройств и фиксированных IP из EEPROM
  loadAliasesFromEEPROM();
  
  // Настройка WiFi
  setupWiFiAP();
  
  // Инициализация DHCP сервера
  initDHCPServer();
  
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
  Serial.println("DHCP server started on port 67");
  Serial.println("Ready to receive MPU6050 data from VR headsets!");
  
  // Первое сканирование
  scanNetwork();
  
  Serial.println("Setup completed successfully!");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  webSocketWeb.loop();
  
  // Обработка DHCP запросов
  handleDHCP();
  
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    scanNetwork();
  }
  
  // Буферизованное сохранение в EEPROM
  if (eepromDirty && (millis() - lastEEPROMSave >= EEPROM_SAVE_INTERVAL)) {
    saveAliasesToEEPROM();
  }
  
  // Периодическая проверка восстановления
  if (millis() - lastRecoveryCheck > RECOVERY_CHECK_INTERVAL) {
    recoveryCheck();
  }
  
  delay(100);
}
