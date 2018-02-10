#define DEBUG_MODE false

#include <mcp_can.h>
#include <SPI.h>

#include <SdFat.h>
SdFat SD;

#include <Wire.h>
#include <RTClib.h>

// Serial parameters
#define SERIAL_BAUD_RATE    250000          // Faster is better, as CAN is frequently ~500 Kbps

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

// Efficiently store CAN data in a struct for fast binary writing
volatile struct CANMessage {
  unsigned int t;
  unsigned long int id;
  unsigned char data[8];
} m;

// This computes the maximum number of times we can write
// before needing to flush the contents to the SD
const int maxWriteCount = 512 / sizeof(m);
int writeCounter = 0;

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
  char filename[] = "MMDDHHMM.DAT";
  
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
  
  // Initialize MCP2515 with masks and filters disabled
  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK) {
    Serial.println("MCP2515 init failure");
  }
  CAN0.setMode(CAN_MODE);
  pinMode(CAN_INTERRUPT_PIN, INPUT);
  
}

void loop() {

  // Read data when CAN interrupt pin is low
  if(!digitalRead(CAN_INTERRUPT_PIN)) {
    
    // Read the data, store it in the struct, continuing until all messages are received
    unsigned char len = 0;
    
    while (CAN0.readMsgBuf(&m.id, len, m.data) == CAN_OK) {
      
      // Write the struct in binary to the SD card
      writeCounter++;
      dataFile.write((const uint8_t *)&m, sizeof(m));
      
      // Only flush the file when the buffer is full
      if (writeCounter >= maxWriteCount) {
        dataFile.flush();
        writeCounter = 0;
      }
    }
    
  }
  
}
