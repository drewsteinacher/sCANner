#define DEBUG_MODE false

#include <mcp_can.h>
#include <SPI.h>

#include <SdFat.h>
SdFat SD;

#include <Wire.h>
#include <RTClib.h>

// Serial parameters
#define SERIAL_BAUD_RATE    9600          // Faster is better, as CAN is frequently ~500 Kbps

// RTC parameters
#define RTC_INTERRUPT_PIN   3               // Pin for a 1 Hz square wave from the RTC
RTC_DS1307 rtc;
DateTime now;

// SD parameters
#define SD_CHIP_SELECT      10              // Adafruit = 10, Ethernet = 4
File dataFile;

// CAN parameters
#define CAN_INTERRUPT_PIN    2              // Pin to know when messages are ready
#define CAN_MODE             MCP_LISTENONLY // MCP_NORMAL sends acks, MCP_LISTENONLY does not
MCP_CAN CAN0(9);                            // Chip select is 9 for SeeedStudio shield 1.1+

#define LOG_INTERVAL_SECONDS 1
#define ECHO_TO_SERIAL      1

#define REPORT(x)                 dataFile.print(x); if(ECHO_TO_SERIAL) Serial.print(x);
#define REPORT_LINE(x)            dataFile.println(x); if(ECHO_TO_SERIAL) Serial.println(x);
#define REPORT_FORM(x, form)      dataFile.print(x, form); if(ECHO_TO_SERIAL) Serial.print(x, form);
#define REPORT_LINE_FORM(x, form) dataFile.println(x, form); if(ECHO_TO_SERIAL) Serial.println(x, form);
#define COMMA                     REPORT(",")

// Efficiently store CAN data in a struct for fast binary writing
volatile struct CANMessage {
  unsigned int t;
  unsigned long int id;
  unsigned char data[8];
} m;

// Structure to more easily keep track of quantities
class CANQuantity {
  public:
  String label;
  unsigned long int id;
  float (*foo)(CANMessage*);
  int counter;
  float value;
  CANQuantity(String l, unsigned long int ulong, float (*f)(CANMessage*)) {
    label = l;
    id = ulong;
    foo = f;
  };
  void update(float temp) {
    value = (temp + counter * value) / (counter + 1);
    counter = counter + 1;
  };
  void bar(CANMessage * message) {
    update((*foo)(message));
  };
  void reset() {
    counter = 0;
    value = 0.0;
  }

};


float updateSpeedometer(CANMessage &message) {
  return 0.05625 * 0.621 * ((message.data)[0] + 256. * ((message.data)[1]));
}

float updateOdometer(CANMessage &message) {
  return 0.1 * (message.data[0] + 256. * (message.data[1]) + 65536. * message.data[2]);
}

float updateEngineSpeed(CANMessage &message) {
  return 0.260417 * (message.data[3] + 256. * message.data[2]) - 3901.3;
}

float updateFuelFlow(CANMessage &message) {
  return 0.000951019 * (message.data[0] + 256. * (message.data[1]));
}

float updateAmbientTemp(CANMessage &message) {
  return 50.5 + 0.178571 * message.data[0];
}

float updateEngineTemp(CANMessage &message) {
  return 1.8 * (message.data[2] - 40) + 32.;
}

float updateCruiseSpeed(CANMessage &message) {
  return message.data[7];
}

float updateDistanceToEmpty(CANMessage &message) {
  return 10. * message.data[7];
}

#define QUANTITY_COUNT  8
CANQuantity quantities[QUANTITY_COUNT] = {
  CANQuantity("Speed",  0xD1, updateSpeedometer),
  CANQuantity("Odometer",  0x6D1, updateOdometer),
  CANQuantity("EngineSpeed", 0x144, updateEngineSpeed),
  CANQuantity("FuelFlow", 0x360, updateFuelFlow),
  CANQuantity("AmbientTemp", 0x3D1, updateAmbientTemp),
  CANQuantity("EngineTemp", 0x360, updateEngineTemp),
  CANQuantity("CruiseSpeed", 0x360, updateCruiseSpeed),
  CANQuantity("DistanceToEmpty", 0x156, updateDistanceToEmpty)
};

// This is the ISR for the 1 Hz square wave pulse
inline void updateTime() {
  m.t++;
}

// Report errors to serial port and freeze evaluation
void error(char *str) {
  
  Serial.print("Error: ");
  Serial.println(str);
  
  // Keep LED on to indicates error state
  digitalWrite(LED_BUILTIN, HIGH);
  while(1);
}

void setup() {
  
  Serial.begin(SERIAL_BAUD_RATE);
  
  // Initialize RTC, construct filename (MMDDHHMM.CSV)
  Wire.begin();
  if (!rtc.begin()) error("RTC failure");
  
  // Avoid bad file trouble when car cuts power when starting
  delay(1000);

  now = rtc.now();
  char filename[] = "MMDDHHMM.CSV";
  
  // Fill MM, DD, HH and MM via the RTC
  uint8_t month = now.month();
  uint8_t day = now.day();
  uint8_t hour = now.hour();
  uint8_t minute = now.minute();
  
  // Round to nearest minute
  if (minute < 59 && now.second() > 30) minute++;
  
  filename[0] = month / 10 + '0';
  filename[1] = month % 10 + '0';
  filename[2] = day / 10 + '0';
  filename[3] = day % 10 + '0';
  filename[4] = hour / 10 + '0';
  filename[5] = hour % 10 + '0';
  filename[6] = minute / 10 + '0';
  filename[7] = minute % 10 + '0';
  
  // Set up square wave interrupt to count seconds since file creation
  pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), updateTime, RISING);
  rtc.writeSqwPinMode(SquareWave1HZ);
  m.t = 0;
  
  // Set up SD card
  if (!SD.begin(SD_CHIP_SELECT)) error("SD failure");
  
  // Overwrite the existing file
  dataFile = SD.open(filename, O_RDWR | O_TRUNC | O_CREAT /*O_WRITE | O_CREAT */ /*FILE_WRITE | O_TRUNC*/);
  if (!dataFile) error("File open failure");

  // Add file headers
  REPORT("Time"); COMMA;
  for (int i = 0; i < QUANTITY_COUNT; i++) {
    REPORT(quantities[i].label);
    if (i < QUANTITY_COUNT - 1) {COMMA;}
  }
  REPORT_LINE();
  
  // Initialize MCP2515 with masks and filters disabled
  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK) {
    Serial.println("MCP2515 init failure");
  }
  CAN0.setMode(CAN_MODE);
  pinMode(CAN_INTERRUPT_PIN, INPUT);
  
}


unsigned int lastWriteTime = 0;
unsigned char len = 0;

void loop() {
  
  // Read data when CAN interrupt pin is low
  while (!digitalRead(CAN_INTERRUPT_PIN) && CAN0.readMsgBuf(&m.id, len, m.data) == CAN_OK) {

    // Only log certain messages    
    for (int i = 0; i < QUANTITY_COUNT; i++) {
      if (m.id == quantities[i].id) {
        quantities[i].bar(&m);
      }
    }
    
  }
  
  // Write to the SD card
  if (m.t % LOG_INTERVAL_SECONDS == 0 && m.t >= lastWriteTime + ((LOG_INTERVAL_SECONDS + 1) / 2) {
    
    REPORT(m.t);
    COMMA;
    
    for (int i = 0; i < QUANTITY_COUNT; i++) {
      REPORT(quantities[i].value);
      quantities[i].reset();
      if (i < QUANTITY_COUNT - 1) {COMMA;}
    }
    
    
    REPORT_LINE();
    dataFile.flush();
    
    lastWriteTime = m.t;
  }
  
}
