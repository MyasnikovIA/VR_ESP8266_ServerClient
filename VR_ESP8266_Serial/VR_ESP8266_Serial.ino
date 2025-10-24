#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

// Sensor data
float pitch = 0, roll = 0, yaw = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
bool calibrated = false;
unsigned long lastTime = 0;

// Относительный ноль
float zeroPitchOffset = 0, zeroRollOffset = 0, zeroYawOffset = 0;
bool zeroSet = false;

// Накопленные углы (без ограничений)
double accumulatedPitch = 0, accumulatedRoll = 0, accumulatedYaw = 0;
float prevPitch = 0, prevRoll = 0, prevYaw = 0;
bool firstMeasurement = true;

// Настройки вывода данных
const unsigned long PRINT_INTERVAL = 100; // Вывод каждые 100 мс

// Функция для обнуления всех углов
void resetAllAngles() {
  pitch = 0;
  roll = 0;
  yaw = 0;
  accumulatedPitch = 0;
  accumulatedRoll = 0;
  accumulatedYaw = 0;
  prevPitch = 0;
  prevRoll = 0;
  prevYaw = 0;
  firstMeasurement = true;
  
  Serial.println("All angles reset to zero");
}

// Функция для установки текущего положения как нулевой точки
void setCurrentPositionAsZero() {
  zeroPitchOffset = accumulatedPitch;
  zeroRollOffset = accumulatedRoll;
  zeroYawOffset = accumulatedYaw;
  zeroSet = true;
  
  Serial.println("Current position set as zero point");
  Serial.print("Zero Pitch Offset: "); Serial.print(zeroPitchOffset, 2);
  Serial.print(" Roll Offset: "); Serial.print(zeroRollOffset, 2);
  Serial.print(" Yaw Offset: "); Serial.println(zeroYawOffset, 2);
}

// НОВАЯ ФУНКЦИЯ: Обнуление точек поворота
void resetZeroOffsets() {
  zeroPitchOffset = 0;
  zeroRollOffset = 0;
  zeroYawOffset = 0;
  zeroSet = false;
  
  Serial.println("Zero offsets reset to zero");
}

// Сброс относительного нуля
void resetZeroPoint() {
  zeroPitchOffset = 0;
  zeroRollOffset = 0;
  zeroYawOffset = 0;
  zeroSet = false;
  
  Serial.println("Zero point reset");
}

// Расчет накопленных углов (без ограничений)
void updateAccumulatedAngles() {
  if (firstMeasurement) {
    prevPitch = pitch;
    prevRoll = roll;
    prevYaw = yaw;
    firstMeasurement = false;
    return;
  }
  
  // Вычисляем разницу углов с учетом переходов через 180/-180 для ВСЕХ осей
  float deltaPitch = pitch - prevPitch;
  float deltaRoll = roll - prevRoll;
  float deltaYaw = yaw - prevYaw;
  
  // Корректируем разницу для переходов через границу ±180 для ВСЕХ осей
  if (deltaPitch > 180) deltaPitch -= 360;
  else if (deltaPitch < -180) deltaPitch += 360;
  
  if (deltaRoll > 180) deltaRoll -= 360;
  else if (deltaRoll < -180) deltaRoll += 360;
  
  if (deltaYaw > 180) deltaYaw -= 360;
  else if (deltaYaw < -180) deltaYaw += 360;
  
  // Накопление углов для ВСЕХ осей
  accumulatedPitch += deltaPitch;
  accumulatedRoll += deltaRoll;
  accumulatedYaw += deltaYaw;
  
  prevPitch = pitch;
  prevRoll = roll;
  prevYaw = yaw;
}

// Получение относительных углов (относительно зафиксированного нуля)
double getRelativePitch() {
  if (!zeroSet) return accumulatedPitch;
  return accumulatedPitch - zeroPitchOffset;
}

double getRelativeRoll() {
  if (!zeroSet) return accumulatedRoll;
  return accumulatedRoll - zeroRollOffset;
}

double getRelativeYaw() {
  if (!zeroSet) return accumulatedYaw;
  return accumulatedYaw - zeroYawOffset;
}

// НОВАЯ ФУНКЦИЯ: Визуализация абсолютного угла в виде прогресс-бара
String visualizeAngle(double angle, int length = 20) {
  String bar = "[";
  int center = length / 2;
  
  // Нормализуем угол к диапазону -180..180 для визуализации
  double normalizedAngle = fmod(angle, 360.0);
  if (normalizedAngle > 180) normalizedAngle -= 360;
  else if (normalizedAngle < -180) normalizedAngle += 360;
  
  // Позиция маркера на шкале
  int markerPos = map(normalizedAngle, -180, 180, 0, length - 1);
  markerPos = constrain(markerPos, 0, length - 1);
  
  for (int i = 0; i < length; i++) {
    if (i == center) {
      bar += "|";  // Центральная метка
    } else if (i == markerPos) {
      bar += "o";  // Маркер текущего угла
    } else {
      bar += " ";
    }
  }
  bar += "]";
  return bar;
}

void calibrateSensor() {
  Serial.println("Calibrating MPU6050...");
  float sumX = 0, sumY = 0, sumZ = 0;
  
  for (int i = 0; i < 500; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sumX += g.gyro.x;
    sumY += g.gyro.y;
    sumZ += g.gyro.z;
    delay(2);
  }
  
  gyroOffsetX = sumX / 500;
  gyroOffsetY = sumY / 500;
  gyroOffsetZ = sumZ / 500;
  calibrated = true;
  
  Serial.println("Calibration complete");
  Serial.print("Gyro offsets - X: "); Serial.print(gyroOffsetX, 6);
  Serial.print(" Y: "); Serial.print(gyroOffsetY, 6);
  Serial.print(" Z: "); Serial.println(gyroOffsetZ, 6);
}

void printSensorData() {
  // Обновляем накопленные углы
  updateAccumulatedAngles();
  
  // Получаем относительные углы
  double relPitch = getRelativePitch();
  double relRoll = getRelativeRoll();
  double relYaw = getRelativeYaw();
  
  Serial.println("=== SENSOR DATA ===");
  
  // Относительные углы
  Serial.println("RELATIVE ANGLES:");
  Serial.print("Pitch: "); Serial.print(relPitch, 2); Serial.print("°  ");
  Serial.println(visualizeAngle(relPitch));
  
  Serial.print("Roll:  "); Serial.print(relRoll, 2); Serial.print("°  ");
  Serial.println(visualizeAngle(relRoll));
  
  Serial.print("Yaw:   "); Serial.print(relYaw, 2); Serial.print("°  ");
  Serial.println(visualizeAngle(relYaw));
  
  // Абсолютные углы
  Serial.println("ABSOLUTE ANGLES:");
  Serial.print("Pitch: "); Serial.print(accumulatedPitch, 2); Serial.print("°  ");
  Serial.println(visualizeAngle(accumulatedPitch));
  
  Serial.print("Roll:  "); Serial.print(accumulatedRoll, 2); Serial.print("°  ");
  Serial.println(visualizeAngle(accumulatedRoll));
  
  Serial.print("Yaw:   "); Serial.print(accumulatedYaw, 2); Serial.print("°  ");
  Serial.println(visualizeAngle(accumulatedYaw));
  
  // Информация о нулевых точках
  Serial.println("ZERO POINTS:");
  Serial.print("Status: "); Serial.println(zeroSet ? "SET" : "NOT SET");
  if (zeroSet) {
    Serial.print("Pitch Offset: "); Serial.print(zeroPitchOffset, 2); Serial.println("°");
    Serial.print("Roll Offset:  "); Serial.print(zeroRollOffset, 2); Serial.println("°");
    Serial.print("Yaw Offset:   "); Serial.print(zeroYawOffset, 2); Serial.println("°");
  }
  
  Serial.println("===================");
}

void processSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    Serial.print("Command received: ");
    Serial.println(command);
    
    if (command == "RESET_ANGLES" || command == "RA") {
      resetAllAngles();
    }
    else if (command == "SET_ZERO" || command == "SZ") {
      setCurrentPositionAsZero();
    }
    else if (command == "RESET_ZERO" || command == "RZ") {
      resetZeroPoint();
    }
    // НОВАЯ КОМАНДА: Обнуление точек поворота
    else if (command == "RESET_OFFSETS" || command == "RO") {
      resetZeroOffsets();
    }
    else if (command == "CALIBRATE" || command == "CAL") {
      calibrated = false;
      calibrateSensor();
    }
    else if (command == "HELP" || command == "?") {
      Serial.println("Available commands:");
      Serial.println("  RESET_ANGLES or RA - Reset all angles to zero");
      Serial.println("  SET_ZERO or SZ - Set current position as zero point");
      Serial.println("  RESET_ZERO or RZ - Reset zero point");
      Serial.println("  RESET_OFFSETS or RO - Reset zero offsets to zero");
      Serial.println("  CALIBRATE or CAL - Recalibrate sensor");
      Serial.println("  HELP or ? - Show this help");
    }
    else {
      Serial.println("Unknown command. Type HELP for available commands.");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("MPU6050 Sensor Data Logger Starting...");
  
  // Инициализация I2C и MPU6050
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1);
  }
  
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
  
  Serial.println("MPU6050 initialized successfully");
  
  // Калибровка сенсора
  calibrateSensor();
  
  Serial.println("MPU6050 Sensor Data Logger started");
  Serial.println("Type HELP for available commands");
  Serial.println("=================================");
}

void loop() {
  // Обработка команд из серийного порта
  processSerialCommands();
  
  if (!calibrated) return;
  
  // Чтение данных с MPU6050
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  unsigned long currentTime = millis();
  float deltaTime = (currentTime - lastTime) / 1000.0;
  if (lastTime == 0) deltaTime = 0.01;
  lastTime = currentTime;
  
  float gyroX = g.gyro.x - gyroOffsetX;
  float gyroY = g.gyro.y - gyroOffsetY;
  float gyroZ = g.gyro.z - gyroOffsetZ;
  
  // ВСЕ оси обновляются ТОЛЬКО данными гироскопа для бесконечного суммирования
  pitch += gyroX * deltaTime * 180.0 / PI;  // Только гироскоп для Pitch
  roll += gyroY * deltaTime * 180.0 / PI;   // Только гироскоп для Roll
  yaw += gyroZ * deltaTime * 180.0 / PI;    // Только гироскоп для Yaw
  
  // Ограничение углов для отображения в диапазоне -180 до 180 градусов
  // Только для визуального отображения, накопленные углы бесконечны
  if (pitch > 180) pitch -= 360;
  else if (pitch < -180) pitch += 360;
  
  if (roll > 180) roll -= 360;
  else if (roll < -180) roll += 360;
  
  if (yaw > 180) yaw -= 360;
  else if (yaw < -180) yaw += 360;
  
  // Вывод данных с интервалом
  static unsigned long lastPrint = 0;
  if (currentTime - lastPrint >= PRINT_INTERVAL) {
    printSensorData();
    lastPrint = currentTime;
  }
  
  delay(10);
}
