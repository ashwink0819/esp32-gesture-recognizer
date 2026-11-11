// MPU6050 raw reader for ESP32 - no library, so clone chips don't get rejected.
// Wiring: VCC->3.3V, GND->GND, SDA->GPIO21, SCL->GPIO22
// Open Serial Plotter at 115200 baud. Keep the board STILL for the first ~3 s (gyro calibration).

#include <Wire.h>

const uint8_t MPU = 0x68;
const uint32_t SAMPLE_US = 10000;   // 100 Hz
float gxOff = 0, gyOff = 0, gzOff = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool readRaw(int16_t v[7]) {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);                          // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU, (uint8_t)14) != 14) return false;
  for (int i = 0; i < 7; i++) v[i] = (Wire.read() << 8) | Wire.read();
  return true;                               // ax ay az temp gx gy gz
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  writeReg(0x6B, 0x00);   // wake up (chip boots in sleep mode)
  writeReg(0x1A, 0x03);   // DLPF ~44 Hz, cuts noise
  writeReg(0x1B, 0x00);   // gyro  +-250 dps  -> 131 LSB/(deg/s)
  writeReg(0x1C, 0x00);   // accel +-2 g      -> 16384 LSB/g

  Wire.beginTransmission(MPU);
  Wire.write(0x75);       // WHO_AM_I
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, (uint8_t)1);
  Serial.printf("WHO_AM_I = 0x%02X (genuine = 0x68; clones often differ - that's OK)\n", Wire.read());

  // Gyro offset calibration - board must be still
  int16_t v[7];
  const int N = 500;
  for (int i = 0; i < N; i++) {
    if (readRaw(v)) { gxOff += v[4]; gyOff += v[5]; gzOff += v[6]; }
    delay(3);
  }
  gxOff /= N; gyOff /= N; gzOff /= N;
  Serial.printf("Gyro offsets: %.1f %.1f %.1f\n", gxOff, gyOff, gzOff);
}

void loop() {
  static uint32_t next = micros();
  if ((int32_t)(micros() - next) < 0) return;
  next += SAMPLE_US;

  int16_t v[7];
  if (!readRaw(v)) { Serial.println("I2C read failed"); return; }

  // Units: accel in g, gyro in deg/s. Labels make Serial Plotter show named lines.
    Serial.printf("%.3f,%.3f,%.3f,%.2f,%.2f,%.2f\n",
    v[0] / 16384.0, v[1] / 16384.0, v[2] / 16384.0,
    (v[4] - gxOff) / 131.0, (v[5] - gyOff) / 131.0, (v[6] - gzOff) / 131.0);
}