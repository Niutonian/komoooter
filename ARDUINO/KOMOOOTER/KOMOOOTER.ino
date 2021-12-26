
/**
 * Komoooter project by @Niutonian
 * https://niutonian.com/komoooter
 * adapted from palto42's code for the ssd1306 display https://github.com/palto42/komoot-navi
 * Forked from @spattinson https://github.com/spattinson/komoot-eink-navigator
*/

#include <SPI.h> // Included for SFE_LSM9DS0 library
#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
//#include <BfButton.h>
#include "BLEDevice.h"
#include "esp_adc_cal.h"
#include <driver/adc.h>

//#define OSW // tft_eSPI >> #include <User_Setups/Setup46_GC9A01_ESP32.h>
#define TTGO_DISPLAY  //tft_eSPI >> #include <User_Setups/Setup25_TTGO_T_Display.h>

#include "rider.h"
#include "symbols.h"
#include "MyFont.h"
#include "calibri14pt.h"

//fonts
#define GFXFF 1
#define MYFONT32 &myFont32pt8b //Big bold font
#define CF_AB30 &c__windows_fonts_calibrib14pt8b //smaller bold font
#define CF_OL24 &Orbitron_Light_24
#define CF_OL32 &Orbitron_Light_32

#if defined( OSW)
#define batteryPin 15
#define battPin  25
//Buttons
#define menuBtn 10
#define resetBtn 13
#elif defined( TTGO_DISPLAY)
#define batteryPinB 34 // FIX THIS
#define battPinB  14
//Buttons
#define menuBtn 0
#define resetBtn 35
#endif

#define TFT_GREY 0x5AEB // New colour

TFT_eSPI tft = TFT_eSPI();  // Invoke library

std::string value = "Start";
int timer = 0 ;
// The remote service we wish to connect to.
static BLEUUID serviceUUID("71C1E128-D92F-4FA8-A2B2-0F171DB3436C");
static BLEUUID    charUUID("503DD605-9BCB-4F6E-B235-270A57483026");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

const char titleText[] = "KOMOOOTER";
const char btText[] = "BLUETOOTH";
const char shutdownText[] = "shutting down";
const char sleepingText[] = "sleeping";
const char connectText[] = "Waiting";
const char connectTextB[] = "for Bluetooth";
const char connectTextBT[] = "Waiting 4 BT";
const char connectedText[] = "Connected";
const char notFoundText[] = "not found";
const char happyText[] = " :)";
const char oohNoText[] = "( ! )";


int charge = 0;
unsigned int raw = 0;
float volt = 0.0;
// ESP32 ADV is a bit non-linear
const float vScale1 = 30; // divider for higher voltage range - OSW
const float vScale1B = 333; // divider for higher voltage range -  TTGO DISPLAY

int menuSetup = 0;//switch menu
long interval = 60000;  // interval to display battery voltage
long previousMillis = 0; // used to time battery update

int timeout = 0;
int resetTime = 0;


//Deep sleep:
bool _sleep = false;
unsigned long timedelay;


// distance and streets
std::string old_street = "";
uint8_t dir = 255;
uint32_t dist2 = 4294967295;
std::string street;
std::string firstWord;
std::string old_firstWord;
bool updated;

//Deep sleep:
void IRAM_ATTR isr() {
  detachInterrupt(digitalPinToInterrupt(resetBtn)); //because later used for wake up
  //setting flag instead to get a little bit of delay
  _sleep = true;
  timedelay = millis();
}


static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  //  Serial.print("Notify callback for characteristic ");
  //  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  //  Serial.print(" of data length ");
  //  Serial.println(length);
  //  Serial.print("data: ");
  //  Serial.println((char*)pData);
}

class MyClientCallback : public BLEClientCallbacks {

    void onConnect(BLEClient* pclient) {
    }

    void onDisconnect(BLEClient* pclient) {
      connected = false;
      Serial.println("onDisconnect");
    }
};

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());
  BLEClient* pClient = BLEDevice::createClient();
  Serial.println(" - Created client");
  pClient->setClientCallbacks(new MyClientCallback());
  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    if (pRemoteCharacteristic->canNotify()) {
      pRemoteCharacteristic->registerForNotify(notifyCallback);
      Serial.println(" - Registered for notifications");
      connected = true;
      return true;
      tft.fillRect(0, 0, 240, 240, TFT_BLACK );
    }
    Serial.println("Failed to register for notifications");
  } else {
    Serial.println("Failed to read our characteristic");
  }

  pClient->disconnect();
  return false;
}
/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());

      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;
      }
    }
};

//DISPLAY START
void tftStartup() {
  //  tft.setRotation(3); //orientation set to 1 to flip the display
  tft.setTextDatum(MC_DATUM);
  tft.setSwapBytes(true);
#if defined( OSW)
  tft.fillRect(0, 0, 240, 120, TFT_WHITE);
  tft.pushImage(45, 5, riderWidth, riderHeight, rider);
  tft.setFreeFont(CF_OL24);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(titleText, 120, 95, GFXFF);
  tft.fillRect(0, 120, 240, 100, TFT_BLACK);
  tft.setFreeFont(CF_OL32);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(connectText, 120, 135, GFXFF); // old street
  tft.setFreeFont(CF_OL24);
  tft.drawString(connectTextB, 120, 175, GFXFF); // old street
#elif defined( TTGO_DISPLAY)
  tft.fillRect(0, 0, 240, 135, TFT_WHITE);
  tft.pushImage(45, -5, riderWidth, riderHeight, rider);
  tft.setFreeFont(CF_OL24);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(titleText, 120, 45, GFXFF);
  tft.fillRect(0, 95, 240, 40, TFT_BLACK);
  tft.setFreeFont(CF_OL32);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(connectTextBT, 120, 105, GFXFF);
#endif

}

void btNotFound() {
  tft.setTextDatum(MC_DATUM);
#if defined( OSW)
  tft.fillRect(0, 0, 240, 120, TFT_WHITE);
  tft.setFreeFont(MYFONT32);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(oohNoText, 120, 30, GFXFF);
  tft.setFreeFont(CF_OL24);
  tft.fillRect(0, 120, 240, 80, TFT_BLACK);
  tft.drawString(btText, 120, 95, GFXFF);
  tft.setFreeFont(CF_OL32);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(notFoundText, 120, 135, GFXFF);
  tft.setFreeFont(CF_OL24);
  tft.drawString(shutdownText, 120, 175, GFXFF);
#elif defined( TTGO_DISPLAY)
  tft.fillRect(0, 0, 240, 135, TFT_WHITE);
  tft.setFreeFont(MYFONT32);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(oohNoText, 120, 30, GFXFF);
  tft.setFreeFont(CF_OL24);
  tft.drawString(btText, 120, 95, GFXFF);
  delay(1500);
  tft.setFreeFont(CF_OL32);
  tft.fillRect(0, 0, 240, 135, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(notFoundText, 120, 50, GFXFF);
  tft.setFreeFont(CF_OL24);
  tft.drawString(sleepingText, 120, 90, GFXFF);
  delay(1500);
#endif

}
//DIRECTION DISPLAY SYMBOLS
void showPartialUpdate_dir(uint8_t dir) {
#if defined( OSW)
  tft.fillRect(0, 0, 240, 65, TFT_WHITE );
  tft.drawBitmap(95, 0, symbols[dir].bitmap, 60, 60, 0);
#elif defined( TTGO_DISPLAY)
  tft.fillRect(0, 0, 80, 60, TFT_WHITE );
  tft.drawBitmap(10, 0, symbols[dir].bitmap, 60, 60, 0);
#endif

}
//STREET NAMES
void showPartialUpdate_street(std::string street, std::string old_street ) {
  tft.setFreeFont(CF_AB30);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
#if defined( OSW)
  tft.fillRect(0, 110, 240, 80, TFT_BLACK);
  tft.drawString(old_street.c_str(), 120, 170, GFXFF); // old street
  tft.drawString(street.c_str(), 120, 130, GFXFF); // new street
#elif defined( TTGO_DISPLAY)
  tft.fillRect(0, 60, 240, 75, TFT_BLACK);
  tft.drawString(old_street.c_str(), 120, 70, GFXFF); // old street
  tft.drawString(street.c_str(), 120, 95, GFXFF); // new street
#endif

}

//METER TO NEXT UPDATE
void showPartialUpdate_dist(uint32_t dist) {
  tft.setFreeFont(MYFONT32);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
#if defined( OSW)
  tft.fillRect(0, 65, 240, 50, TFT_WHITE);
  tft.setCursor(55, 113);
  tft.print(random(10, 300));
  //  tft.print(dist);
  tft.print("m");
#elif defined( TTGO_DISPLAY)
  tft.fillRect(80, 0, 160, 60, TFT_WHITE);
  tft.setCursor(80, 55);
  tft.print(dist);
  tft.print("m");
#endif

}


//BATTERY MANAGEMENT

void batteryLayout() {

  Serial.print ("Battery = ");
  Serial.println (volt);
  Serial.print ("Raw = ");
  Serial.println (raw);
#if defined( OSW)
  charge = digitalRead(batteryPin);
#elif defined( OSW)
  charge = digitalRead(batteryPinB);
#endif

  Serial.print("charge: ");
  Serial.println(charge);
  batteryCharge(charge);

}

void batteryCharge(int charging) {
  tft.setFreeFont(CF_AB30);
  if (charging == 0) {
    tft.setTextColor(TFT_WHITE);
#if defined( OSW)
    tft.fillRect(0, 210, 240, 45, TFT_BLACK);
    tft.setCursor(90, 230);
    tft.print(volt);
    tft.print("V");
#elif defined( TTGO_DISPLAY)
    tft.fillRect(90, 113, 75, 25, TFT_BLACK);
    tft.setCursor(95, 133);
    tft.print(volt);
    tft.print("V");
#endif

  }
  else if (charging == 1) {
    tft.setTextColor(TFT_BLACK);
#if defined( OSW)
    tft.fillRect(0, 210, 240, 45, TFT_WHITE);
    tft.setCursor(90, 230);
    tft.print(volt);
    tft.print("V");
#elif defined( TTGO_DISPLAY)
    tft.fillRect(90, 113, 75, 25, TFT_WHITE);
    tft.setCursor(95, 133);
    tft.print(volt);
    tft.print("V");
#endif
  }
}


void getVolts(void * parameter) {
  for (;;) { // infinite loop
#if defined( OSW)
    raw  = (float) analogRead(battPin);
    volt = raw / vScale1;
#elif defined( TTGO_DISPLAY)
    raw  = (float) analogRead(battPinB);
    volt = raw / vScale1B;
#endif
    //volt = ((float)raw / 4095.0) * 2.0 * 3.3 * (1100 / 1000.0);
    //    batteryLayout();
    delay(56 * 1000);
  }
}


void setup() {
  // enable debug output
  Serial.begin(115200);
#if defined( OSW)
  pinMode(battPin, INPUT); //Battery voltage pin
  pinMode(batteryPin, INPUT);// Battery charge pin
#elif defined( TTGO_DISPLAY)
  pinMode(battPinB, INPUT); //Battery voltage pin
  pinMode(batteryPinB, INPUT);// Battery charge pin
#endif

  pinMode(resetBtn, INPUT_PULLDOWN); //button upper right
  pinMode(menuBtn, INPUT_PULLDOWN); //MenuBtn for future project
  //Deep sleep setup:
  _sleep = false;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);



  //are on in sleep, otherwise use 10kOhm external resistor for pull down
  Serial.println("--");
  attachInterrupt(digitalPinToInterrupt(resetBtn), isr, RISING);

  //  littleFSconfig();

  Serial.println("-Init display-");
  tft.init();

#if defined( OSW)
  tft.setRotation(0);
  tftStartup();
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC_ATTEN_DB_2_5, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
  raw  = analogRead(battPin);
  volt = raw / vScale1;
  Serial.print ("Battery = ");
  Serial.println (volt);
  Serial.print ("Raw = ");
  Serial.println (raw);
  raw  = (float) analogRead(battPin);
  volt = raw / vScale1;//Comment out
  batteryLayout();
#elif defined( TTGO_DISPLAY)
  tft.setRotation(1);
  tftStartup();
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC_ATTEN_DB_2_5, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
  raw  = analogRead(battPinB);
  volt = raw / vScale1B;
  Serial.print ("Battery = ");
  Serial.println (volt);
  Serial.print ("Raw = ");
  Serial.println (raw);
  raw  = (float) analogRead(battPinB);
  volt = raw / vScale1B;//Comment out #endif
#endif

  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("Komoooter");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(20, false);

} // End of setup.


void loop() {
  btLoop();
  batteryLayout();

}


void btLoop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  //Deep sleep:
  if (_sleep && millis() - timedelay > 1000) {
    Serial.println("Going to sleep now");
#if defined( OSW)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_10, 1); //1 = High, 0 = Low connected to GPIO32
    esp_deep_sleep_start();
#elif defined( TTGO_DISPLAY)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 1); //1 = High, 0 = Low connected to GPIO32
    esp_deep_sleep_start();
#endif
  }


  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      tft.setTextDatum(MC_DATUM);
      tft.setFreeFont(CF_AB30);
      tft.setTextColor(TFT_WHITE);
#if defined( OSW)
      tft.fillRect(0, 0, 240, 240, TFT_BLACK);
      tft.drawString(connectedText, 120, 80, GFXFF); // old street
      tft.setFreeFont(MYFONT32);
      tft.drawString(happyText, 120, 130, GFXFF); // old street
#elif defined( TTGO_DISPLAY)
      tft.fillRect(0, 0, 240, 135, TFT_BLACK);
      tft.drawString(connectedText, 120, 95, GFXFF); // old street
      tft.setFreeFont(MYFONT32);
      tft.drawString(happyText, 120, 35, GFXFF); // old street
#endif
      delay(1000);
    } else {
      Serial.println("We have failed to connect to the server; there is nothing more we will do.");
    }
    doConnect = false;
  }


  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
#if defined( OSW)
    raw  = (float) analogRead(battPin);
    volt = raw / vScale1;
#elif defined( TTGO_DISPLAY)
    raw  = (float) analogRead(battPinB);
    volt = raw / vScale1B;
#endif
    int batterybackground = map(volt, 0, 5, 0, 255);
    Serial.print("batterybackground");
    Serial.println(batterybackground);

  }

  if (connected) {
    std::string value = pRemoteCharacteristic->readValue();//this crashes sometimes, receives the whole data packet
    if (value.length() > 4) {
      //in case we have update flag but characteristic changed due to navigation stop between
      updated = false;
      street = value.substr(9);//this causes abort when there are not at least 9 bytes available
      if (street != old_street) {
        old_street = street;
        old_firstWord = firstWord;
        firstWord = street.substr(0, street.find(", "));
        showPartialUpdate_street(firstWord, old_firstWord);
        Serial.print("Street update: ");
        Serial.println(firstWord.c_str());
        updated = true;
      } //extracts the firstword of the street name and displays it

      std::string direction;
      direction = value.substr(4, 4);
      uint8_t d = direction[0];
      if (d != dir) {
        dir = d;
        showPartialUpdate_dir(dir);
        Serial.print("Direction update: ");
        Serial.println(d);
        updated = true;
      } //display direction

      std::string distance;
      distance = value.substr(5, 8);
      uint32_t dist = distance[0] | distance[1] << 8 | distance[2] << 16 | distance[3] << 24;
      if (dist2 != dist) {
        dist2 = dist;
        showPartialUpdate_dist(dist2);
        Serial.print("Distance update: ");
        Serial.println(dist2);
        updated = true;
      } //display distance in metres

      if (dist2 > 100) {
        esp_sleep_enable_timer_wakeup(4000000);
        delay(4000);
      } else {
        delay(2000);
      }
    } else if (doScan) {
      BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
    }
  } else {
    btNotFound();
    resetTime = millis() - resetTime;
    Serial.print("Scan time: ");
    Serial.println(resetTime / 1000);
    //GO TO BED
    if (resetTime > 1000) {
      delay(2000);
      tft.fillScreen(TFT_BLACK);
      delay(200);
      esp_deep_sleep_start();
    }
    delay(1000);
  }
} // End of loop
