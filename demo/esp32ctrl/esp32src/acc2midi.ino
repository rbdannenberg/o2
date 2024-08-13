// Include SparkFunLSM9DS1 library and its dependencies
#include <Wire.h>
#include <SPI.h>
#include <SparkFunLSM9DS1.h>
#include <WiFi.h>
//#include <WiFiUdp.h>
#include "o2lite.h"

// First, check if push button is down (pull pin 2 low).
// If so, we are the wand sensor, o.w. sword.
// Then, connect to WiFi: blink LED at 1hz until connected.
//    network name = Aylesboro, password 4125214147,

const unsigned int SAMPLE_PERIOD = 100;

#define VERBOSE if (0)

// WiFi network name and password:
const char *networkName = "Aylesboro";
const char *networkPswd = "1234567890";
const char *HOSTNAME = "esp";
const int HOSTPORT = 8001;

// Internet domain to send to:
const char *SERVER_IP = "192.168.1.201";
const int SERVER_PORT = 8011;

const int BUTTON_PIN = 0;
const int LED_PIN = 5;
const int PUSH_PIN = 2;

const int NOTE_ON = 0x90;

float imu_ax = 0;
float imu_ay = 0;
float imu_az = 0;
float imu_gx = 0;
float imu_gy = 0;
float imu_gz = 0;

float beat_time = 0;
float beat_dur = 0.5;
float prev_pitch = -1;

float limit = 1.0;
float thresh = 0.0;
int min_pitch = 50;
int max_pitch = 70;
int scale = 1;

int scalemap[4][12] = {
    {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}, // major
    {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0}, // (natural) minor
    {1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0}, // blues
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}  // chromatic 
};


void set_limit(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    limit = o2l_get_float();
    Serial.println("/midiapp/limit "); Serial.println(limit);
}


void set_thresh(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    thresh = o2l_get_float();
    Serial.println("/midiapp/thresh "); Serial.println(thresh);
}


void set_prange(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    min_pitch = o2l_get_int32();
    max_pitch = o2l_get_int32();
    Serial.println("/midiapp/prange "); Serial.print(min_pitch);
    Serial.println(" "); Serial.print(max_pitch);
}


void set_dur(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    beat_dur = o2l_get_float();
    Serial.println("/midiapp/dur "); Serial.println(beat_dur);
}


void set_scale(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    int s = o2l_get_int32();
    Serial.println("/midiapp/scale "); Serial.println(s);
    if (s >= 1 && s <= 4) {
        scale = s;
    }
}


void set_blink(o2l_msg_ptr msg, const char *types, void *data, void *info)
{
    void flash();  // use flash in o2liteesp32.cpp
    flash();  // blink LED 100ms
}


void setup()
{
  // Initilize hardware:
  Serial.begin(115200);
  Serial.println("calling setMinSecurity");
  WiFi.setMinSecurity(WIFI_AUTH_WEP);
  connect_to_wifi("esp", networkName, networkPswd);
  o2l_initialize("rbdapp");
  o2l_set_services("midiapp");
  // Map from -axlimit to +axlimit to the pitch range
  o2l_method_new("/midiapp/limit", "f", true, set_limit, NULL);
  // set threshold for on/off
  o2l_method_new("/midiapp/thresh", "f", true, set_thresh, NULL);
  // set pitch range
  o2l_method_new("/midiapp/prange", "ii", true, set_prange, NULL);
  // set duration
  o2l_method_new("/midiapp/dur", "f", true, set_dur, NULL);
  // blink
  o2l_method_new("/midiapp/blink", "", true, set_blink, NULL);
  // scale
  o2l_method_new("/midiapp/scale", "i", true, set_scale, NULL);
  Wire.begin();
  imu_setup();
}


void send_note(int pitch, int vel)
{
    // adjust the pitch until it is in the current scale
    int pc = pitch % 12;
    // scales are numbered 1 to 4, but index is 0 to 3:
    while (!scalemap[scale - 1][pc]) {
       pitch++;
       pc = pitch % 12;
    }
    // transpose out-of-range pitch if necessary:
    while (pitch > 127) pitch -= 12;;
    while (pitch < 0) pitch += 12;
    o2l_send_start("/midiout/midi", 0, "i", 1);  // tcp
    o2l_add_int32(NOTE_ON + (pitch << 8) + (vel << 16));
    o2l_send();
}


// this is the "compositional algorithm" that constructs
// MIDI output from sensor data
void compute_beat()
{
    int pitch;
    if (prev_pitch > 0) {
        send_note(prev_pitch, 0);  // note off
    }
    if (imu_ay > thresh) {
        pitch = 0;  // disabled by roll
    } else {
        // use imu_ax for pitch and imu_ay for on/off
        if (imu_ax < -limit) imu_ax = -limit;
        else if (imu_ax > limit) imu_ax = limit;
        pitch = (max_pitch - min_pitch) * (limit - imu_ax) / (2 * limit) +
                min_pitch;
        send_note(pitch, 100);  // note on
        Serial.print("noteon ");
        Serial.print(pitch);
        Serial.print(" ");
        Serial.println(o2l_local_time());
    }
    prev_pitch = pitch;
}

void showtime(const char *msg) {
    Serial.print(msg); Serial.print(" "); Serial.print(millis() / 1000.0);
    Serial.print("; ");
}

long prev_loop_start = 0;
long longest_period = 0;
long period_reset = 0;

void loop()
{
    long loop_start = millis();
    long period = loop_start - prev_loop_start;
    // every 20 sec, reset longest_period so we print something
    if (loop_start > period_reset + 20000) {
      longest_period = 0;
      period_reset = loop_start;
    }
    // when we see a longer period than before, print it
    if (period > longest_period) {
      Serial.print("period: "); Serial.println(period * 0.001);
      longest_period = period;
    } 
    prev_loop_start = loop_start;
    o2l_poll();
    imu_poll();
    double now = o2l_local_time();
    if (beat_time < now) {
        compute_beat();
        // advance to next beat AFTER current time
        while (beat_time < now) {
            beat_time += beat_dur;
        }
    }
}


LSM9DS1 imu;  // Create an LSM9DS1 object

// Mag address must be 0x1E, would be 0x1C if SDO_M is LOW
#define LSM9DS1_M   0x1E
// Accel/gyro address must be 0x6B, would be 0x6A if SDO_AG is LOW
#define LSM9DS1_AG  0x6B

// Global variables to keep track of update rates
unsigned long startTime;
unsigned int accelReadCounter = 0;
unsigned int gyroReadCounter = 0;
unsigned int magReadCounter = 0;
unsigned int tempReadCounter = 0;

// Global variables to print to serial monitor at a steady rate
unsigned long lastPrint = 0;

void setupDevice()
{
  // Use IMU_MODE_I2C
  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;
}

void setupGyro()
{
  // [enabled] turns the gyro on or off.
  imu.settings.gyro.enabled = true;  // Enable the gyro
  // [scale] sets the full-scale range of the gyroscope.
  // scale can be set to either 245, 500, or 2000
  imu.settings.gyro.scale = 245; // Set scale to +/-245dps
  // [sampleRate] sets the output data rate (ODR) of the gyro
  // sampleRate can be set between 1-6
  // 1 = 14.9    4 = 238
  // 2 = 59.5    5 = 476
  // 3 = 119     6 = 952
  imu.settings.gyro.sampleRate = 3; // 59.5Hz ODR
  // [bandwidth] can set the cutoff frequency of the gyro.
  // Allowed values: 0-3. Actual value of cutoff frequency
  // depends on the sample rate. (Datasheet section 7.12)
  imu.settings.gyro.bandwidth = 0;
  // [lowPowerEnable] turns low-power mode on or off.
  imu.settings.gyro.lowPowerEnable = false; // LP mode off
  // [HPFEnable] enables or disables the high-pass filter
  imu.settings.gyro.HPFEnable = true; // HPF disabled
  // [HPFCutoff] sets the HPF cutoff frequency (if enabled)
  // Allowable values are 0-9. Value depends on ODR.
  // (Datasheet section 7.14)
  imu.settings.gyro.HPFCutoff = 1; // HPF cutoff = 4Hz
  // [flipX], [flipY], and [flipZ] are booleans that can
  // automatically switch the positive/negative orientation
  // of the three gyro axes.
  imu.settings.gyro.flipX = false; // Don't flip X
  imu.settings.gyro.flipY = false; // Don't flip Y
  imu.settings.gyro.flipZ = false; // Don't flip Z
}

void setupAccel()
{
  // [enabled] turns the acclerometer on or off.
  imu.settings.accel.enabled = true; // Enable accelerometer
  // [enableX], [enableY], and [enableZ] can turn on or off
  // select axes of the acclerometer.
  imu.settings.accel.enableX = true; // Enable X
  imu.settings.accel.enableY = true; // Enable Y
  imu.settings.accel.enableZ = true; // Enable Z
  // [scale] sets the full-scale range of the accelerometer.
  // accel scale can be 2, 4, 8, or 16
  imu.settings.accel.scale = 8; // Set accel scale to +/-8g.
  // [sampleRate] sets the output data rate (ODR) of the
  // accelerometer. ONLY APPLICABLE WHEN THE GYROSCOPE IS
  // DISABLED! Otherwise accel sample rate = gyro sample rate.
  // accel sample rate can be 1-6
  // 1 = 10 Hz    4 = 238 Hz
  // 2 = 50 Hz    5 = 476 Hz
  // 3 = 119 Hz   6 = 952 Hz
  imu.settings.accel.sampleRate = 1; // Set accel to 10Hz.
  // [bandwidth] sets the anti-aliasing filter bandwidth.
  // Accel cutoff freqeuncy can be any value between -1 - 3. 
  // -1 = bandwidth determined by sample rate
  // 0 = 408 Hz   2 = 105 Hz
  // 1 = 211 Hz   3 = 50 Hz
  imu.settings.accel.bandwidth = 0; // BW = 408Hz
  // [highResEnable] enables or disables high resolution 
  // mode for the acclerometer.
  imu.settings.accel.highResEnable = false; // Disable HR
  // [highResBandwidth] sets the LP cutoff frequency of
  // the accelerometer if it's in high-res mode.
  // can be any value between 0-3
  // LP cutoff is set to a factor of sample rate
  // 0 = ODR/50    2 = ODR/9
  // 1 = ODR/100   3 = ODR/400
  imu.settings.accel.highResBandwidth = 0;  
}

void setupMag()
{
  // [enabled] turns the magnetometer on or off.
  imu.settings.mag.enabled = true; // Enable magnetometer
  // [scale] sets the full-scale range of the magnetometer
  // mag scale can be 4, 8, 12, or 16
  imu.settings.mag.scale = 12; // Set mag scale to +/-12 Gs
  // [sampleRate] sets the output data rate (ODR) of the
  // magnetometer.
  // mag data rate can be 0-7:
  // 0 = 0.625 Hz  4 = 10 Hz
  // 1 = 1.25 Hz   5 = 20 Hz
  // 2 = 2.5 Hz    6 = 40 Hz
  // 3 = 5 Hz      7 = 80 Hz
  imu.settings.mag.sampleRate = 5; // Set OD rate to 20Hz
  // [tempCompensationEnable] enables or disables 
  // temperature compensation of the magnetometer.
  imu.settings.mag.tempCompensationEnable = false;
  // [XYPerformance] sets the x and y-axis performance of the
  // magnetometer to either:
  // 0 = Low power mode      2 = high performance
  // 1 = medium performance  3 = ultra-high performance
  imu.settings.mag.XYPerformance = 3; // Ultra-high perform.
  // [ZPerformance] does the same thing, but only for the z
  imu.settings.mag.ZPerformance = 3; // Ultra-high perform.
  // [lowPowerEnable] enables or disables low power mode in
  // the magnetometer.
  imu.settings.mag.lowPowerEnable = false;
  // [operatingMode] sets the operating mode of the
  // magnetometer. operatingMode can be 0-2:
  // 0 = continuous conversion
  // 1 = single-conversion
  // 2 = power down
  imu.settings.mag.operatingMode = 0; // Continuous mode
}

void setupTemperature()
{
  // [enabled] turns the temperature sensor on or off.
  imu.settings.temp.enabled = true;
}

uint16_t initLSM9DS1()
{
  setupDevice(); // Setup general device parameters
  setupGyro(); // Set up gyroscope parameters
  setupAccel(); // Set up accelerometer parameters
  setupMag(); // Set up magnetometer parameters
  setupTemperature(); // Set up temp sensor parameter

  return imu.begin(LSM9DS1_AG, LSM9DS1_M, Wire);
}

void imu_setup() 
{
  Serial.println("Initializing the LSM9DS1");
  uint16_t status = initLSM9DS1();
  Serial.print("LSM9DS1 WHO_AM_I's returned: 0x");
  Serial.println(status, HEX);
  Serial.println("Should be 0x683D");
  Serial.println();

  startTime = millis();
}

void imu_poll() 
{  
  // imu.accelAvailable() returns 1 if new accelerometer
  // data is ready to be read. 0 otherwise.
  if (imu.accelAvailable()) {
    imu.readAccel();
    accelReadCounter++;
  }

  // imu.accelAvailable() returns 1 if new accelerometer
  // data is ready to be read. 0 otherwise.
  if (imu.accelAvailable()) {
    imu.readAccel();
    accelReadCounter++;
  }

  // imu.gyroAvailable() returns 1 if new gyroscope
  // data is ready to be read. 0 otherwise.
  if (imu.gyroAvailable()) {
    imu.readGyro();
    gyroReadCounter++;
  }

  // imu.magAvailable() returns 1 if new magnetometer
  // data is ready to be read. 0 otherwise.
  if (imu.magAvailable()) {
    imu.readMag();
    magReadCounter++;
  }

  // imu.tempAvailable() returns 1 if new temperature sensor
  // data is ready to be read. 0 otherwise.
  if (imu.tempAvailable()) {
    imu.readTemp();
    tempReadCounter++;
  }

  // Every SAMPLE_PERIOD milliseconds, print sensor data:
  if ((lastPrint + SAMPLE_PERIOD) < millis()) {
    printSensorReadings();
    lastPrint = millis();
  }
}

// printSensorReadings prints the latest IMU readings
// along with a calculated update rate.
void printSensorReadings()
{
  float runTime = (float)(millis() - startTime) / 1000.0;
  VERBOSE {
    float accelRate = (float)accelReadCounter / runTime;
    float gyroRate = (float)gyroReadCounter / runTime;
    float magRate = (float)magReadCounter / runTime;
    float tempRate = (float)tempReadCounter / runTime;
    Serial.print("A: ");
    Serial.print(imu.calcAccel(imu.ax));
    Serial.print(", ");
    Serial.print(imu.calcAccel(imu.ay));
    Serial.print(", ");
    Serial.print(imu.calcAccel(imu.az));
    Serial.print(" g \t| ");
    Serial.print(accelRate);
    Serial.println(" Hz");
    Serial.print("G: ");
    Serial.print(imu.calcGyro(imu.gx));
    Serial.print(", ");
    Serial.print(imu.calcGyro(imu.gy));
    Serial.print(", ");
    Serial.print(imu.calcGyro(imu.gz));
    Serial.print(" dps \t| ");
    Serial.print(gyroRate);
    Serial.println(" Hz");
    Serial.print("M: ");
    Serial.print(imu.calcMag(imu.mx));
    Serial.print(", ");
    Serial.print(imu.calcMag(imu.my));
    Serial.print(", ");
    Serial.print(imu.calcMag(imu.mz));
    Serial.print(" Gs \t| ");
    Serial.print(magRate);
    Serial.println(" Hz");
    Serial.print("T: ");
    Serial.print(imu.temperature);
    Serial.print(" \t\t\t| ");
    Serial.print(tempRate);
    Serial.println(" Hz");  
    Serial.println();
  }

  imu_ax = imu.calcAccel(imu.ax);
  imu_ay = imu.calcAccel(imu.ay);
  imu_az = imu.calcAccel(imu.az);
  imu_gx = imu.calcGyro(imu.gx);
  imu_gy = imu.calcGyro(imu.gy);
  imu_gz = imu.calcGyro(imu.gz);
}
