#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <Wire.h>

#include <mcp_can.h>
#include "RTClib.h"

/****************************
 * Configuration parameters *
 ****************************/
#define SYNC_INTERVAL       10000  // Time (in ms) before SD flush
#define SERIAL_BAUD_RATE    250000

#define ECHO_TO_SERIAL      1  // Echo data to serial port
#define WAIT_TO_START       0  // Wait for serial input in setup()
#define WRITE_TO_DISK       1  // Write to SD card

#define SD_CHIP_SELECT_PIN  10 // 10 = Adafruit shield, 4 = Ethernet shield
#define CAN_CHIP_SELECT_PIN 9  // v1.1 = 9, 1.0 and before = 10


/*******************************************************
 * Useful macros to simplify logging via SD and Serial *
 *******************************************************/
#define REPORT(x)                 logfile.print(x); if(ECHO_TO_SERIAL) Serial.print(x);
#define REPORT_LINE(x)            logfile.println(x); if(ECHO_TO_SERIAL) Serial.println(x);
#define REPORT_FORM(x, form)      logfile.print(x, form); if(ECHO_TO_SERIAL) Serial.print(x, form);
#define REPORT_LINE_FORM(x, form) logfile.println(x, form); if(ECHO_TO_SERIAL) Serial.println(x, form);
#define COMMA                     REPORT(",")


/******************************
 * Important global variables *
 ******************************/
MCP_CAN CAN(CAN_CHIP_SELECT_PIN);
RTC_DS1307 RTC;
DateTime now;
File logfile;
uint32_t syncTime = 0;


// Report errors to serial port and freeze evaluation
void error(char *str) {
  
  Serial.print("Error: ");
  Serial.println(str);
  
  // Keep LED on to indicates error state
  digitalWrite(LED_BUILTIN, HIGH);
  while(1);
}



void setup(void) {

  Serial.begin(SERIAL_BAUD_RATE);

  Wire.begin();
  if (!RTC.begin()) {
    error("RTC failed");
  }

  now = RTC.now();
  
#if WAIT_TO_START
  Serial.println(F("Type something to start"));
  while (!Serial.available());
#endif //WAIT_TO_START
  
  // Wait to avoid creating empty files when the car starts
  delay(500);
  
#if WRITE_TO_DISK

  // Initialize SD (pin 10 has to be set as high output)
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);  
  if (!SD.begin(SD_CHIP_SELECT_PIN)) error("SD failed");
  
  // Create new file using numbering scheme
  char filename[] = "YYMMDD00.CSV";
  
  // Fill YY, MM, and DD via the RTC
  filename[0] = (now.year() - 2000) / 10 + '0';
  filename[1] = (now.year() - 2000) % 10 + '0';
  filename[2] = now.month() / 10 + '0';
  filename[3] = now.month() % 10 + '0';
  filename[4] = now.day() / 10 + '0';
  filename[5] = now.day() % 10 + '0';

  // Each day can have up to 99 files
  for (uint16_t i = 0; i < 100; i++) {
    filename[6] = i / 10 + '0';
    filename[7] = i % 10 + '0';
    if (! SD.exists(filename)) {
      logfile = SD.open(filename, FILE_WRITE); 
      break;
    }
  }
  
  if (!logfile) error("Couldnt create file");

#endif // WRITE_TO_DISK

    // Connect to CAN shield
    while (CAN_OK != CAN.begin(CAN_500KBPS)) {
        Serial.println("CAN BUS Shield init fail...");
        delay(100);
    }
}


void loop(void) {
  
  unsigned char len = 0;
  unsigned char buf[8];
  unsigned int canId;
  
  if (CAN_MSGAVAIL == CAN.checkReceive()) {
    
    // Get and report the timestamp
    now = RTC.now();
    REPORT(now.hour());     REPORT(":");
    REPORT(now.minute());   REPORT(":");
    REPORT(now.second());   COMMA;
    
    // Get the data
    CAN.readMsgBuf(&len, buf);
    
    // Get the ID
    canId = CAN.getCanId();
    
    // Report all of the data in comma-separated hex
    REPORT_FORM(canId, HEX); COMMA;
    for (int i = 0; i<len; i++) {
      REPORT_FORM(buf[i], HEX);
      if (i != (len - 1)) COMMA;
    }
    REPORT_LINE();
  }
  
  
  // Write to SD as specified
  if ((millis() - syncTime) >= SYNC_INTERVAL) {
    syncTime = millis();
    logfile.flush();
  }
}
