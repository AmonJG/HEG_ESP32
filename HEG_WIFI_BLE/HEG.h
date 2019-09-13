// Joshua Brewster - HEG Arduino Functions
// Contributors - Diego Schmaedech (StateChanger Team)
// Requires Arduino ADS1x15 library and Arduino ESP32 library, as well as a compatible ESP32 board

// TEST BUILD 0.98
// 9/13/2019
/*
   TODO
   - HRV basic calculation, adjust LED flash rate accordingly (50ms is viable at bare min) Alternatively, get an ECG chip or use the MAX30102?
   - Accurate SpO2 reading?
   - Optimize memory usage. Take better advantage of ESP32.
   - Switch to hardware ISR timer. esp_timer_get_time() in microseconds.
   - figure out how to get rid of delay in loop() to ensure serial input is read. Likely just need to do it only once per sec.
   - ADS1115 library hardlocked at 8ms update rate. Note past 8ms the effective resolution at gain 16 starts to drop below 15 bits with the introduction of noise.
   */


#include <Wire.h>
#include <Adafruit_ADS1015.h>
//#include <esp_timer.h>
//#include <esp_bt.h>
//#include <ArduinoJson.h>

#include "BLE_API.h"

#include <SavLayFilter.h>

double filteredRatio;

SavLayFilter smallFilter(&filteredRatio, 0, 5);  //Cubic smoothing with windowsize of 5
SavLayFilter largeFilter(&filteredRatio, 0, 25); //Cubic smoothing with windowsize of 25

//Slope variables

const int SLOPE_SIZE = 20;
int slopeCounter = 0;
float slopeArray[SLOPE_SIZE];
void setupStateChanger()
{
  for (int x = 0; x < SLOPE_SIZE; x++)
  {
    slopeArray[x] = 0;
  }
}


bool USE_USB = true;          // WRITE 'u' TO TOGGLE, CHANGE HERE TO SET DEFAULT ON POWERING THE DEVICE
bool USE_BT = false;           // WRITE 'b' TO TOGGLE.
bool pIR_MODE = false;        // SET TO TRUE OR WRITE 'p' TO DO PASSIVE INFRARED ONLY (NO RED LIGHT FOR BLOOD-OXYGEN DETECTION). RATIO IS USELESS HERE, USE ADC CHANGES AS MEASUREMENT.
bool NOISE_REDUCTION = false; // WRITE 'n' TO TOGGLE USING 4 LEDS FOR NOISE CANCELLING *EXPERIMENTAL*

bool DEBUG_ESP32 = false;
bool DEBUG_ADC = false; // FOR USE IN ARDUINO IDE
bool DEBUG_LEDS = false;
bool SEND_DUMMY_VALUE = false;

bool BLE_ON, BLE_SETUP = false;

const int8_t ledRate = 25;        // LED flash rate (ms). Can go as fast as 10ms for better heartrate visibility.
const int8_t sampleRate = 2;      // ADC read rate (ms). ADS1115 has a max of 860sps or 1/860 * 1000 ms or 1.16ms. Bluetooth limits it in Arduino to 125sps.
const int8_t samplesPerRatio = 2; // Minimum number of samples per LED to accumulate before making a measurement. Adjust this with your LED rate so you sample across the whole flash at minimum.
const int8_t BTRate = 100;        // Bluetooth notify rate (ms). Min rate should be 10ms, however it will hang up if the buffer is not flushing. 100ms is stable.
const int8_t USBRate = 0;         // No need to delay USB unless on old setups.

const int nSensors = 2;
const int nLEDs = 4;
// LED GPIO pin definitions. Default LOLIN32 Pinout, commented values are TTGO T1 pins.
int RED = 13, IR = 25, REDn = 16, IRn = 4; //Default LED GPIO. n values are LEDs used for noise cancelling.
int IR0 = 25;    // Default left 3cm LEDs
int RED0 = 13; //33
int IR1 = 4;//15              // Left 1cm LEDs
int RED1 = 16;//2
int IR2 = 26;             // Right 1cm LEDs
int RED2 = 27;
int IR3 = 14;             // Right 3cm LEDs
int RED3 = 12;

float scaling = 1;

const int LED = 5;  // Lolin32 V1.0.0 LED on Pin 5
const int PWR = 21;//14//21 // Powers ADC and OPT101

//SET NON-DEFAULT SDA AND SCL PINS
#define SDA0_PIN 23//16//23 //5
#define SCL0_PIN 19//4//19 //18

//For dual i2c or i2c switching
//#define SDA1_PIN ?
//#define SCL1_PIN ?


// HEG VARIABLES
bool coreProgramEnabled = false;
bool adcEnabled = false;
bool reset = false;
bool deviceConnected= false;

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    Serial.println("Device connected to BLE");
    deviceConnected = true;
  }

  void onDisconnect(BLEServer *pServer)
  {
    Serial.println("Device disconnected from BLE");
    deviceConnected = false;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0)
    {

      Serial.println("STATE CHANGER RECEIVE: ");

      for (int i = 0; i < rxValue.length(); i++)
      {
        Serial.print(rxValue[i]);
      }
       
      //commandESP32(rxValue.c_str());

      if (rxValue.find("t") != -1)
      { 
        coreProgramEnabled = true;
        Serial.println("Turning ON!");
      }
      else if (rxValue.find("r") != -1)
      {
        Serial.println("Turning OFF!");
      }
    }
  }
};

void setupBLE(){
  {

    // Create the BLE Device
    BLEDevice::init("STATECHANGER"); // Give it a name

    // Create the BLE Server
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create the BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Create a BLE Characteristic
    pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);

    pCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);

    pCharacteristic->setCallbacks(new MyCallbacks());

    // Start the service
    pService->start();

    // Start advertising
    pServer->getAdvertising()->start();
    BLE_SETUP = true;
    BLE_ON = true;
    USE_BT = true;
    Serial.println("BLE service started, scan for STATECHANGER.");
  }
}



String output;

int16_t adc0; // Resulting 15 bit integer.

//Setup ADS1115
Adafruit_ADS1115 ads(0x48);
long adcChannel = 0; //Channel on the ADC to read. Default 0.

float Voltage = 0.0;
float range = 32767; // 16 bit ADC (15 bits of range)
float gain = 0.256;  // +/- V
float bits2mv = gain / range;

//Signal flags
bool red_led = false; // Bools to alternate LEDS
bool ir_led = false;
bool no_led = true;            // Both LEDS begin off so default is true
bool badSignal = false;        // Bool for too high of an ADC reading
bool signalDetermined = false; // Bool for whether the ADC reading is within desired range

//Counters
int ticks0, redTicks, irTicks, ratioTicks, adcTicks, noLEDTicks = 0;

//Scoring variables
long redValue, irValue, rawValue = 0;
float redGet, irGet, redAvg, irAvg, rawAvg, rednAvg, irnAvg, rawnAvg, ratio, baseline = 0;

float p1, p2, v1, v2, accel = 0;
float ratioAvg, noiseAvg, adcAvg, posAvg, adcnAvg, denoised = 0; //velAvg, accelAvg;

//TX Variables
//char adcString[10], ratioString[10], posString[10], txString[40]; // Faster in BLE mode
//char scoreString[10]


//Timing variables
unsigned long sampleMillis;
unsigned long currentMillis;
unsigned long LEDMillis;
unsigned long BLEMillis;
unsigned long USBMillis;
g
//Start ADC and set gain. Starts timers
void startADS()
{
    // Begin ADS
    ads.begin();
    ads.setGain(GAIN_SIXTEEN);

    //ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV (default)
    //ads.setGain(GAIN_ONE);     // 1x gain   +/- 4.096V  1 bit = 2mV
    //ads.setGain(GAIN_TWO);     // 2x gain   +/- 2.048V  1 bit = 1mV
    //ads.setGain(GAIN_FOUR);    // 4x gain   +/- 1.024V  1 bit = 0.5mV
    //ads.setGain(GAIN_EIGHT);   // 8x gain   +/- 0.512V  1 bit = 0.25mV
    //ads.setGain(GAIN_SIXTEEN); // 16x gain  +/- 0.256V  1 bit = 0.125mV

    adcEnabled = true;
    sampleMillis = millis();
}

void setupHEG() {

  setupStateChanger();
  
  Wire.begin(SDA0_PIN, SCL0_PIN);

  pinMode(IR, OUTPUT);
  pinMode(RED, OUTPUT);

  pinMode(PWR, OUTPUT);
  digitalWrite(PWR, HIGH);

  //LOLIN32 ONLY
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  if(USE_BT == true){
    setupBLE();
  }

  BLEMillis = millis();
  USBMillis = millis();
}

void readADS() {
    adc0 = ads.readADC_SingleEnded(adcChannel);
    Serial.print("ADC Value: ");
    Serial.println(adc0);
}

void sensorTest() { // Test currently selected photodiode and LEDS on 16-bit ADC.
  bool failed = false;

  if (adcEnabled == false) {
    startADS();
  }

  coreProgramEnabled = false;
  digitalWrite(RED, LOW);
  digitalWrite(IR, LOW);
  delay(250);

  for (int j = 0; j < nSensors; j++) {
    adcChannel = j;
    for (int i = 0; i < 10; i++) {
      adc0 = ads.readADC_SingleEnded(adcChannel);

      if (USE_USB == true) {
        Serial.flush();
        Serial.print("Testing photodiode ");
        Serial.print(j);
        Serial.print(" | ADC value: ");
        Serial.println(adc0);
      }
      if (USE_BT == true) {
        /*if (SerialBT.hasClient()) {
          SerialBT.flush();
          SerialBT.print("Testing photodiode " + String(j) + " | ADC value: " + String(adc0) + "\r\n");
        }*/
      }
      if (adc0 == -1) {
        if (USE_USB == true) {
          Serial.println("Error: Check SDA/SCL pin settings and solder connections.");
          Serial.print("SDA pin assigned: ");
          Serial.print(SDA0_PIN);
          Serial.print(" | SCL pin assigned: ");
          Serial.println(SCL0_PIN);
        }
        /*if (USE_BT == true) {
          if (SerialBT.hasClient() == true) {
            SerialBT.print("Error, check SDA/SCL pin settings and solder connections. \r\n");
            SerialBT.print("SDA pin assigned: " + String(SDA0_PIN) + " | SCL pin assigned: " + String(SCL0_PIN) + "\r\n");
          }
        }*/
        failed = true;
      }
      else if ((adc0 > 100) && (adc0 < 32760)) {
        if (USE_USB == true) {
          Serial.println("Photodiode readings within a range that indicates it is working.");
        }
        /*if (USE_BT == true) {
          if (SerialBT.hasClient()) {
            SerialBT.print("Photodiode readings within a range that indicates it is working.\r\n");
          }
        }*/
      }
      else if (adc0 > 32760) {
        if (USE_USB == true) {
          Serial.println("Notice: ADC saturated. Isolate from light or check photodiode pin connections.");
        }
        /*if (USE_BT == true) {
          SerialBT.print("Notice: ADC saturated. Isolate from light or check photodiode pin connections.\r\n");
        }*/
        failed = true;
      }
      delay(100);
    }
  }

  if (failed == true) {
    if (USE_USB == true) {
      Serial.println("Photodiode final test result: FAIL. Resetting...");
    }
    /*if (USE_BT == true) {
      if (SerialBT.hasClient() == true) {
        SerialBT.print("Photodiode final test result: FAIL. Resetting... \r\n");
      }
    }*/
    ESP.restart();
  }
}




void LEDTest() { // Test LEDs assuming photodiodes are good
  bool failed = false;

  if (adcEnabled == false) {
    startADS();
  }

  coreProgramEnabled = false;
  digitalWrite(RED, LOW);
  digitalWrite(IR, LOW);
  delay(250);

  if (USE_USB == true) {
    Serial.flush();
    Serial.println("Testing LEDs...");
  }
  /*if (USE_BT == true) {
    if (SerialBT.hasClient() == true) {
      SerialBT.flush();
      SerialBT.print("Testing LEDs... \r\n");
    }
  }*/

  for (int i = 0; i < nLEDs; i++) {
    if (i == 0) {
      adcChannel = 0;
      RED = RED0;
      IR = IR0;
    }
    if (i == 1) {
      adcChannel = 0; RED = RED1; IR = IR1;
      pinMode(RED, OUTPUT);
      pinMode(IR, OUTPUT);
    }
    if (i == 2) {
      adcChannel = 1; RED = RED2; IR = IR2;
      pinMode(RED, OUTPUT);
      pinMode(IR, OUTPUT);
    }
    if (i == 3) {
      adcChannel = 1; RED = RED3; IR = IR3;
      pinMode(RED, OUTPUT);
      pinMode(IR, OUTPUT);
    }

    for (int j = 0; j < 3; j++) {
      rawValue = ads.readADC_SingleEnded(adcChannel);
      delay(200);
      rawValue = ads.readADC_SingleEnded(adcChannel);
      digitalWrite(RED, HIGH);
      delay(200);
      redValue = ads.readADC_SingleEnded(adcChannel);
      delay(200);
      redValue = ads.readADC_SingleEnded(adcChannel);
      digitalWrite(RED, LOW);
      digitalWrite(IR, HIGH);
      delay(200);
      irValue = ads.readADC_SingleEnded(adcChannel);
      delay(200);
      irValue = ads.readADC_SingleEnded(adcChannel);
      digitalWrite(IR, LOW);
      if (redValue / rawValue < 2) {
        if (USE_USB == true) {
          Serial.flush();
          Serial.print("Error: RED LED difference from ambient insufficient at site: ");
        }
        /*if (USE_BT == true) {
          if (SerialBT.hasClient()) {
            SerialBT.flush();
            SerialBT.print("Error: RED LED difference from ambient insufficient at ");
          }
        }*/
        failed = true;
      }
      if (USE_USB == true) {
        Serial.print("Site: ");
        Serial.println(i);
        Serial.print("Ambient reading: ");
        Serial.print(rawValue);
        Serial.print(" | RED reading: ");
        Serial.println(redValue);
      }
      /*if (USE_BT == true) {
        if (SerialBT.hasClient()) {
          SerialBT.print("Site: " + String(i) + "\r\n" + "Ambient reading: " + String(rawValue) + " | RED reading: " + String(redValue) + "\r\n");
        }
      }*/
      if (irValue / rawValue < 1.3) {
        if (USE_USB == true) {
          Serial.flush();
          Serial.print("Error: IR LED difference from ambience insufficient at ");
        }
        /*if (USE_BT == true) {
          if (SerialBT.hasClient()) {
            SerialBT.flush();
            SerialBT.print("Error: IR LED difference from ambience insufficient at ");
          }
        }*/
        failed = true;
      }
      if (USE_USB == true) {
        Serial.print("Site ");
        Serial.println(i);
        Serial.print("Ambient reading: ");
        Serial.print(rawValue);
        Serial.print(" | IR reading: ");
        Serial.println(irValue);
      }
      /*if (USE_BT == true) {
        if (SerialBT.hasClient()) {
          SerialBT.print("Site: " + String(i) + "\r\n" + "Ambient reading: " + String(rawValue) + " | IR reading: " + String(irValue) + "\r\n");
        }
      }*/
      if (irValue > redValue) {
        if (USE_USB == true) {
          Serial.flush();
          Serial.print("Error: IR LED brighter than RED LED at Site: ");
          Serial.print(i);
          Serial.println(". Adjust headband or check pins.");
        }
        /*if (USE_BT == true) {
          if (SerialBT.hasClient()) {
            SerialBT.flush();
            SerialBT.print("Error: IR LED brighter than RED LED at Site:" + String(i) + ". Adjust sensor or check pins. \r\n");
          }
        }*/
        failed = true;
      }
      delay(500);
    }
  }

  if (failed == true) {
    if (USE_USB == true) {
      Serial.println("LED final test results: FAIL. Resetting...");
    }
    /*if (USE_BT == true) {
      if (SerialBT.hasClient()) {
        SerialBT.print("LED final test results: FAIL. Resetting..  \r\n");
      }
    }*/
    ESP.restart();
  }
}

void check_signal() {
  if ((adc0 >= 6000) || (reset == true))
  { // The gain is high but anything over 6000 is most likely not a valid signal, anything more than 2000 is not likely your body's signal.
    //Serial.println("\nBad Read ");
    badSignal = true;

    //Temp: reset baseline on bad read
    signalDetermined = false;
    reset = false;
    baseline = 0;

    ticks0 = 0; // Reset counter
    redTicks = 0;
    irTicks = 0;
    noLEDTicks = 0;
    redValue = 0; // Reset values
    irValue = 0;
    rawValue = 0;
    redAvg = 0;
    irAvg = 0;
    rawAvg = 0;
    ratioAvg = 0;
    posAvg = 0;
  }
  else if (badSignal == true)
  {
    badSignal = false;
  }
}

void switch_LEDs(int R, int Ir) {
  // Switch LEDs back and forth.
  if (currentMillis - LEDMillis >= ledRate)
  {
    if ((red_led == false))
    { // && (no_led == true)) { // Red on
      if (pIR_MODE == false)
      { // no LEDs in pIR mode, just raw IR from body heat emission.
        digitalWrite(R, HIGH);
        digitalWrite(Ir, LOW);
        red_led = true;
        ir_led = false;
        no_led = false;
        if (DEBUG_LEDS == true)
        {
          Serial.println("RED ON");
        }
      }
    }
    else if (red_led == true)
    { // IR on
      if (pIR_MODE == false)
      {
        digitalWrite(R, LOW);
        digitalWrite(Ir, HIGH);
        red_led = false;
        ir_led = true;
        no_led = false;
        if (DEBUG_LEDS == true)
        {
          Serial.println("IR ON");
        }
      }
    }
    else
    { // No LEDs
      digitalWrite(R, LOW);
      digitalWrite(Ir, LOW);
      red_led = false;
      ir_led = false;
      no_led = true;
      if (DEBUG_LEDS == true)
      {
        Serial.println("NO LED");
      }
    }
    LEDMillis = currentMillis;
  }
}

void get_baseline() {
  // GET BASELINE
  ticks0++;
  if (ticks0 > 100) { // Wait n samples of good signal before getting baseline
    // IR IN 12, RED IN 13
    if ((redTicks < 30) && (irTicks < 30))
    { // && (noLEDTicks < 250)) { // Accumulate samples for baseline
      if (red_led == true)
      { // RED
        redValue += adc0;
        redTicks++;
      }
      else if (ir_led == true)
      { // IR
        irValue += adc0;
        irTicks++;
      }
      else
      {
        rawValue += adc0;
        noLEDTicks++;
      }
      //Serial.println("\nGetting Baseline. . .");
    }
    else
    {
      signalDetermined = true;

      //rawAvg = rawValue / noLEDTicks;
      redAvg = (redValue / redTicks); // - rawAvg);
      irAvg = (irValue / irTicks);    // - rawAvg);

      baseline = ((redAvg) / (irAvg)) * scaling; // Set baseline ratio, multiply by 100 for scaling.
      ratioAvg += baseline;                               // First ratio sent via serial will be baseline.

      redValue = 0; // Reset values
      irValue = 0;
      rawValue = 0;
      ticks0 = 0; // Reset counters
      redTicks = 0;
      irTicks = 0;
      noLEDTicks = 0;
      ratioTicks++;

      //Uncomment this for baseline printing
      //Serial.println("\tBaseline R: ");
      //Serial.print(baseline,4);
    }
  }
}

void get_ratio(bool isNoise, bool getPos) {
  ticks0++;
  if ((red_led == true) && redTicks < samplesPerRatio)
  { // RED
    redValue += adc0;
    redTicks++;
  }
  else if ((ir_led == true)&&(irTicks < samplesPerRatio))
  { // IR
    irValue += adc0;
    irTicks++;
  }
  else
  {
    if(noLEDTicks < samplesPerRatio){
      rawValue += adc0;
      noLEDTicks++;
    }
  }
  if ((redTicks >= samplesPerRatio) && (irTicks >= samplesPerRatio))
  { // && (noLEDTicks >= samplesPerRatio)) { // Accumulate 50 samples per LED before taking reading
    //rawAvg = rawValue / noLEDTicks;
    //Serial.print("Red: ");
    //Serial.print(redValue);
    //Serial.print("|");
    //Serial.println(redTicks);
    //Serial.print("IR: ");
    //Serial.print(irValue);
    //Serial.print("|");
    //Serial.println(irTicks);
    
    redGet = (redValue / redTicks);                  // - rawAvg); // Divide value by number of samples accumulated // Scalar multiplier to make changes more apparent
    irGet = (irValue / irTicks);                     // - rawAvg); // Can filter with log10() applied to each value before dividing.
    ratio = ((redGet) / (irGet)) * scaling; // Get ratio, multiply by 100 for scaling.
    //Serial.println(ratio);
    if (isNoise == false) {
      ratioAvg += ratio;
      redAvg = redGet;
      irAvg = irGet;
    }
    else {
      noiseAvg += ratio;
      rednAvg = redGet;
      irnAvg = irGet;
    }

    if (getPos == true) {
      p1 = p2;
      p2 = ratio - baseline; // Position
      posAvg += p2 - p1;
    }

    //v1 = v2;
    //v2 = (p2 - p1) * ticks0 * 0.001; // Velocity in ms
    //velAvg += v2;

    //accel = (v2 - v1) * ticks0 * 0.001; // Acceleration in ms^2
    //accelAvg += accel;

    //score += ratio-baseline; // Simple scoring method. Better is to compare current and last SMA
    //scoreAvg += score;

    ticks0 = 0; //Reset Counters
    redTicks = 0;
    irTicks = 0;
    noLEDTicks = 0;

    ratioTicks++;

    redValue = 0; //Reset values to get next average
    irValue = 0;
    rawValue = 0;
  }
}

// Core loop for HEG sampling.
void core_program()
{
    if (adcEnabled == false)
    {
      startADS();
      //Start timers
      LEDMillis = millis();
    }
    if (SEND_DUMMY_VALUE != true)
    {
      // Switch LEDs back and forth.
      switch_LEDs(RED, IR);
      if (currentMillis - sampleMillis >= sampleRate)
      {
        // read the analog in value:
        adc0 = ads.readADC_SingleEnded(adcChannel); // -1 indicates something wrong with the ADC
        //Voltage = (adc0 * bits2mv);

        // print the results to the Serial Monitor:
        if (DEBUG_ADC == true)
        {
          Serial.println("ADC Value: ");
          Serial.println(adc0);
          //Serial.println("\tVoltage: ");
          //Serial.println(Voltage,7);
        }
        else
        {
          check_signal();
          if (signalDetermined == false)
          { // GET BASELINE
            get_baseline();
          }
          else
          { // GET RATIO
            get_ratio(false, true);
          }
        }
        sampleMillis = currentMillis;
      }
      adcAvg += adc0;
      adcTicks++;
    }
  //DEBUG_ESP32
  if (DEBUG_ESP32 == true)
  {
    if (USE_USB == true)
    {
      Serial.println("Heap after core_program cycle: ");
      Serial.println(ESP.getFreeHeap());
    }
    /*if (USE_BT == true)
    {
      SerialBT.println("Heap after core_program cycle: ");
      SerialBT.println(ESP.getFreeHeap());
    }*/
  }
} // END core_program()




//if ratioAvg > 0
void noise_reduce() { //Experimental noise cancelling using another set of LEDs
  digitalWrite(RED, LOW);
  digitalWrite(IR, LOW);
  delay(50); // 50ms delay to give LEDs time to shut down fully
  red_led = false;
  ir_led = false;
  noiseAvg = 0;
  while (noiseAvg == 0) {
    switch_LEDs(REDn, IRn);
    if (currentMillis - sampleMillis >= sampleRate)
    {
      // read the analog in value:
      adc0 = ads.readADC_SingleEnded(adcChannel); // -1 indicates something wrong with the ADC
      //Voltage = (adc0 * bits2mv);

      // print the results to the Serial Monitor:
      if (DEBUG_ADC == true)
      {
        Serial.print("ADC Value: ");
        Serial.println(adc0);
        //Serial.print("\tVoltage: ");
        //Serial.println(Voltage,7);
      }
      else
      {
        get_ratio(true, false);
      }
      sampleMillis = currentMillis;
    }
    adcnAvg += adc0;
    //adcTicks++;
  }
  denoised = ratioAvg - noiseAvg;
}

void updateHEG()
{
  if (currentMillis - USBMillis >= USBRate)
  {
    if (adcTicks > 0)
    {
      adcAvg = adcAvg / adcTicks;
      if (ratioTicks > 0)
      {
        ratioAvg = ratioAvg / ratioTicks;
        posAvg = posAvg / ratioTicks;


 /*
StateChanger Header Start//==================================================================================================
 */        filteredRatio = ratioAvg; //set filter

        slopeArray[slopeCounter] = largeFilter.Compute(); //circular buffer
        slopeCounter++;
        float ratioSlope = 0;
        float xAvg = 0;
        float yAvg = 0;
        for (int x = 0; x < SLOPE_SIZE; x++)
        {
          xAvg += x;
          yAvg += slopeArray[x];
        }
        xAvg = xAvg / SLOPE_SIZE;
        yAvg = yAvg / SLOPE_SIZE;
        float v1 = 0;
        float v2 = 0;
        float vAI = 0;
        for (int x = 0; x < SLOPE_SIZE; x++)
        {
          v1 += (x - xAvg) * (slopeArray[x] - yAvg);
          v2 += (float)pow(x - xAvg, 2);
          if (x < (SLOPE_SIZE - 1))
          {
            vAI += (slopeArray[x + 1] - slopeArray[x]) >= 0 ? 1 : 0;
          }
        }
        ratioSlope = abs(v2) > 0 ? (v1 / v2) : 0;
        vAI = (vAI / SLOPE_SIZE) * 100; //ai points
        if (slopeCounter == 20)
        {
          slopeCounter = 0;
        }
        /*
StateChanger Header Start//==================================================================================================
 */
        //outputValue = ratioAvg;
        //output = "NO DATA";
        if (NOISE_REDUCTION == true)
        { 
          output = String(currentMillis) + "|" + String(rednAvg) + "|" + String(irnAvg) + "|" + String(noiseAvg, 4) + "|" + String(denoised, 4) + "|" + String(largeFilter.Compute(), 4) + "|" + String(adcAvg, 0) + "|" + String(posAvg, 4) + "|" + String(ratioSlope, 4) + "|" + String(vAI, 4) + "\r\n";
        }
        else
        {
          output = String(currentMillis) + "|" + String(redAvg) + "|" + String(irAvg) + "|" + String(ratioAvg, 4) + "|" + String(smallFilter.Compute(), 4) + "|" + String(largeFilter.Compute(), 4) + "|" + String(adcAvg, 0) + "|" + String(posAvg, 4) + "|" + String(ratioSlope, 4) + "|" + String(vAI, 4) + "\r\n";
        }
        
        if (USE_USB == true)
        {
          Serial.flush();
          Serial.println(output);
        }
        
        /*if (USE_BT == true)
        {
          if (SerialBT.hasClient() == true)
          {
            SerialBT.flush();
            SerialBT.print(String(redAvg) + "|" + String(irAvg) + "|" + String(ratioAvg, 4) + "|" + String(smallFilter.Compute(), 4) + "|" + String(largeFilter.Compute(), 4) + "|" + String(adcAvg, 0) + "|" + String(posAvg, 4) + "\r\n");
            delay(10); // 10ms delay min required for BT output only due to bottleneck in library.
          }
        }*/
      }

      rawAvg = 0;
      ratioAvg = 0;
      posAvg = 0;
      adcAvg = 0;
      adc0 = 0;
      ratioTicks = 0;
      adcTicks = 0;
    }
    USBMillis = currentMillis;
  }
}

void HEG_core_loop()
{
  currentMillis = millis();;
  if(coreProgramEnabled == true){
    core_program();
  }
  if (NOISE_REDUCTION == true) {
    if (ratioAvg > 0) {
      noise_reduce();
    }
  }
  updateHEG(); 
}