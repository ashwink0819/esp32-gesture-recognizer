// On-device gesture recognition: ESP32 + MPU6050 + Edge Impulse.
// Waits for motion, captures a 2 s window (same as training), classifies it.
#include <gesture-sensor_inferencing.h>
#include <Wire.h>

const uint8_t MPU = 0x68;
const float CONF_THRESHOLD = 0.80;   // below this -> "uncertain", do nothing
const float TRIGGER_DPS = 30.0;      // gyro speed that starts a capture
const int PRE_SAMPLES = 60;          // keep 0.6 s from before the trigger

float buf[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];   // 200 samples x 6 axes
float ring[PRE_SAMPLES][6];
int ringPos = 0;
float gxOff = 0, gyOff = 0, gzOff = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

bool readSample(float s[6]) {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU, (uint8_t)14) != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; i++) v[i] = (Wire.read() << 8) | Wire.read();
  // Same units as training data: accel in g, gyro in deg/s
  s[0] = v[0] / 16384.0; s[1] = v[1] / 16384.0; s[2] = v[2] / 16384.0;
  s[3] = (v[4] - gxOff) / 131.0; s[4] = (v[5] - gyOff) / 131.0; s[5] = (v[6] - gzOff) / 131.0;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);
  Wire.setClock(400000);
  writeReg(0x6B, 0x00); writeReg(0x1A, 0x03); writeReg(0x1B, 0x00); writeReg(0x1C, 0x00);

  Serial.println("Calibrating gyro - keep the board STILL...");
  int16_t v[7]; const int N = 500;
  for (int i = 0; i < N; i++) {
    Wire.beginTransmission(MPU); Wire.write(0x43); Wire.endTransmission(false);
    Wire.requestFrom(MPU, (uint8_t)6);
    gxOff += (int16_t)((Wire.read() << 8) | Wire.read());
    gyOff += (int16_t)((Wire.read() << 8) | Wire.read());
    gzOff += (int16_t)((Wire.read() << 8) | Wire.read());
    delay(3);
  }
  gxOff /= N; gyOff /= N; gzOff /= N;
  Serial.println("Ready. Do a gesture.");
}

void loop() {
  static uint32_t next = micros();
  float s[6];

  // 1. Wait for motion, keeping the last 0.3 s in a ring buffer
  while (true) {
    while ((int32_t)(micros() - next) < 0) {}
    next += EI_CLASSIFIER_INTERVAL_MS * 1000;
    if (!readSample(s)) { Serial.println("I2C read failed"); return; }
    memcpy(ring[ringPos], s, sizeof(s));
    ringPos = (ringPos + 1) % PRE_SAMPLES;
    float gyroMag = sqrtf(s[3]*s[3] + s[4]*s[4] + s[5]*s[5]);
    if (gyroMag > TRIGGER_DPS) break;
  }

  // 2. Build the 2 s window: 0.3 s before the trigger + 1.7 s after
  int idx = 0;
  for (int i = 0; i < PRE_SAMPLES; i++) {
    float *r = ring[(ringPos + i) % PRE_SAMPLES];
    for (int a = 0; a < 6; a++) buf[idx++] = r[a];
  }
  while (idx < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
    while ((int32_t)(micros() - next) < 0) {}
    next += EI_CLASSIFIER_INTERVAL_MS * 1000;
    if (!readSample(s)) continue;
    for (int a = 0; a < 6; a++) buf[idx++] = s[a];
  }

  // 3. Classify
  signal_t signal;
  numpy::signal_from_buffer(buf, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  ei_impulse_result_t result = { 0 };
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) {
    Serial.println("Classifier error"); return;
  }

  size_t best = 0;
  for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    if (result.classification[i].value > result.classification[best].value) best = i;

  const char *label = result.classification[best].label;
  float conf = result.classification[best].value;

  if (conf < CONF_THRESHOLD)          Serial.printf("uncertain (best: %s %.0f%%)\n", label, conf * 100);
  else if (strcmp(label, "idle") == 0) Serial.printf("idle (%.0f%%)\n", conf * 100);
  else                                 Serial.printf(">>> %s  (%.0f%%)\n", label, conf * 100);

  // 4. Short cooldown so the hand returning to rest isn't classified again
  delay(800);
  next = micros();
}