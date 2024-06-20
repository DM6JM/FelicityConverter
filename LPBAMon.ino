/**************************************************/
/* Felicity LPBA48100-OL data converter           */
/**************************************************/


/******************************/
/* Includes                   */
/******************************/
// Arduino Lib - "ESP32-TWAI-CAN by sorek.uk" (https://www.arduino.cc/reference/en/libraries/esp32-twai-can/)
#include <ESP32-TWAI-CAN.hpp>
// Arduino Lib - "Modbus by UL DARA" (https://github.com/uldara1/Modbus)
#include <Modbus.h>

/******************************/
/* Defines                    */
/******************************/

#define V2PINOUT

#ifdef V2PINOUT

#define LED_MQ_PIN 23
#define LED_BAT_PIN 22

#define CAN_TX_PIN GPIO_NUM_33 
#define CAN_RX_PIN GPIO_NUM_27 

#define CAN_EN_PIN 32

#define RS485_RX_PIN 16
#define RS485_TX_PIN 17
// The pin is also used as !RE since both are connected
#define RS485_DE_PIN 21

#else /* V1PINOUT ***************************/

#define LED_MQ_PIN 23
#define LED_BAT_PIN 22

#define CAN_TX_PIN GPIO_NUM_12 //12 is also MDTI of JTAG
#define CAN_RX_PIN GPIO_NUM_13 //13 is also MTCK of JTAG

#define CAN_EN_PIN 27

#define RS485_RX_PIN 16
#define RS485_TX_PIN 17
// The pin is also used as !RE since both are connected
#define RS485_DE_PIN 23  //V1 21, V1_1 23

#endif

#define RS485 Serial2

//MODBUS ID to use (as sniffed from EDMS Tool)
#define CLIENTID 0x01
#define CLIENTID_ALT 0x10
// MODBUS command
#define READHOLDING 0x03

#define BATREG_VERSION 0xF80B
#define BATLEN_VERSION 0x01

#define BATREG_CELLINFO 0x132A
#define BATLEN_CELLINFO 0x14  // Battery doesn't have 8 sensors, so just query the existing 4 (0x18-4=0x14)

#define BATREG_LIMITS 0x131C
#define BATLEN_LIMITS 0x04

#define BATREG_INFO 0x1302
#define BATLEN_INFO 0x0A

#define BATRS485BAUD 9600 //As defined in PDF
#define VICTRONBMSCANBAUD 500E3 // BMS CAN @ Victron

#define NUMBEROFCELLS 16
#define NUMBEROFTEMPS 4

#define BATREADOK_VERSION 1
#define BATREADOK_CELLINFO 2
#define BATREADOK_LIMITS 4
#define BATREADOK_INFO 8

#define BATREADOK_ALL (BATREADOK_VERSION + BATREADOK_CELLINFO + BATREADOK_LIMITS + BATREADOK_INFO)

#define MAXBATIDFAILS 20
/******************************/
/* Typedefs                   */
/******************************/

typedef uint8_t batReadOutOk_t; 

/******************************/
/* Variables                  */
/******************************/
float cellVoltages[NUMBEROFCELLS];
float cellTemperatures[NUMBEROFTEMPS];

int battVersion;
int battStatus;
int battFaults;
int battSOC;

float battVoltage;
float battCurrent;
float battPCBTemperature;


float battLimitVoltageCharge;
float battLimitCurrentCharge;
float battLimitVoltageDischarge;
float battLimitCurrentDischarge;

batReadOutOk_t batReadOk;
bool batDataOk = false;
bool batAltIDuse = false;
int batIDFails = 0;
int batClientID = CLIENTID;

Modbus batterybus(RS485);

CanFrame rxFrame;
unsigned int can305Count = 0;
bool canSendMakesSense = false;

long mainLoopTimer = 0;

bool consoleValuesOn = false;
/******************************/
/* Constants                  */
/******************************/


/******************************/
/* Arduino default functions  */
/******************************/
void setup() {
  // Setup code to run once

  // Debug output
  Serial.begin(115200);
  while (!Serial);

  Serial.println("LPBAMon booting...");

  // Map can pins
  //CAN.setPins(CAN_RX_PIN, CAN_TX_PIN);
  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, 1);

  // Start the CAN bus
  // if (!CAN.begin(VICTRONBMSCANBAUD)) {
  //   Serial.println("Starting CAN failed!");
  //   while (1);
  // }
  if(ESP32Can.begin(ESP32Can.convertSpeed(500), CAN_TX_PIN, CAN_RX_PIN, 10, 10)) {
      Serial.println("CAN bus started!");
  } else {
      Serial.println("CAN bus failed!");
  }
  

  // Register the receive callback
  // We send out our "addon"-packets after the battery has sent theirs
  //CAN.onReceive(onCANReceive);

  RS485.begin(BATRS485BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  batterybus.init(RS485_DE_PIN);

  pinMode(LED_BAT_PIN, OUTPUT);
  pinMode(LED_MQ_PIN, OUTPUT);

  batDataOk = false;
  consoleValuesOn = false;
}

void loop() {
  // Main code to run

  // You can set custom timeout, default is 1000
  if(ESP32Can.readFrame(rxFrame, 5)) {
    switch(rxFrame.identifier)
    {
      case 0x305:
      {
        can305Count++;  

        if(can305Count == 0) // Once running, prevent value of 0 caused by overflow
        {
          can305Count++;
        }
        break;    
      }
      case 0x307:
      {
        
        break;
      }
      case 0x0351:
      {
        // Serial.printf("Received frame 351h DVCC: %02X%02X %02X%02X %02X%02X %02X%02X\r\n", rxFrame.data[0], rxFrame.data[1], rxFrame.data[2], rxFrame.data[3], rxFrame.data[4], rxFrame.data[5], rxFrame.data[6], rxFrame.data[7]);
        break;
      }
      case 0x0355:
      {
        // Serial.printf("Received frame 355h State: %02X%02X %02X%02X %02X%02X %02X%02X\r\n", rxFrame.data[0], rxFrame.data[1], rxFrame.data[2], rxFrame.data[3], rxFrame.data[4], rxFrame.data[5], rxFrame.data[6], rxFrame.data[7]);
        break;
      }
      case 0x0356:
      {
        // Serial.printf("Received frame 356h Stats: %02X%02X %02X%02X %02X%02X %02X%02X\r\n", rxFrame.data[0], rxFrame.data[1], rxFrame.data[2], rxFrame.data[3], rxFrame.data[4], rxFrame.data[5], rxFrame.data[6], rxFrame.data[7]);
        break;
      }
      case 0x0359:
      {
        // Serial.printf("Received frame 359h Protection/Alarm: %02X%02X %02X%02X %02X%02X %02X%02X\r\n", rxFrame.data[0], rxFrame.data[1], rxFrame.data[2], rxFrame.data[3], rxFrame.data[4], rxFrame.data[5], rxFrame.data[6], rxFrame.data[7]);
        break;
      }
      case 0x035C:
      {
        // Serial.printf("Received frame 35Ch ChgReq: %02X%02X %02X%02X %02X%02X %02X%02X\r\n", rxFrame.data[0], rxFrame.data[1], rxFrame.data[2], rxFrame.data[3], rxFrame.data[4], rxFrame.data[5], rxFrame.data[6], rxFrame.data[7]);
        break;
      }
      case 0x035E:
      {
        // Serial.print("Received frame 35Eh Name: ");
        // Serial.println((char *)rxFrame.data);
        break;
      }
      case 0x035F:
      {
        //Serial.printf("Received frame 35Fh Info: %02X%02X %02X%02X %02X%02X %02X%02X\r\n", rxFrame.data[0], rxFrame.data[1], rxFrame.data[2], rxFrame.data[3], rxFrame.data[4], rxFrame.data[5], rxFrame.data[6], rxFrame.data[7]);
        break;
      }
      default:
      {
        Serial.printf("Received frame: %03X  \r\n", rxFrame.identifier);
        break;
      }
    }

    if(rxFrame.rtr)
    {
      Serial.printf("Received RTR-frame: %03X  \r\n", rxFrame.identifier);
    }
  }

  //Check if debug console is wanted
  if (Serial.available() > 0) {
    // get incoming byte:
    char cmdbuf[4];
    size_t cmdlen = Serial.readBytesUntil(0xA, cmdbuf, 4);
    
    if(cmdlen == 3)
    {      
      if(cmdbuf[0] == 'C')
      {
        if(cmdbuf[1] == 'V')
        {
          if(cmdbuf[2] == '1')
          {
            Serial.println("Console Values turned ON");
            consoleValuesOn = true;
          }
          if(cmdbuf[2] == '0')
          {
            Serial.println("Console Values turned OFF");
            consoleValuesOn = false;
          }
        }
      }
    }
  }

  // Poll data each 5 seconds
  if((millis()- mainLoopTimer) > 5000)
  {
    mainLoopTimer = millis();
    batReadOk = 0;  

    if(batAltIDuse)
    {
      batClientID = CLIENTID_ALT;
    }
    else
    {
      batClientID = CLIENTID;
    }

    int result = 0;
    // Read Version
    result = batterybus.requestFrom(batClientID, READHOLDING, BATREG_VERSION, BATLEN_VERSION);
    if(result > 0) {
      int version = batterybus.uint16(0);
      battVersion = version;
      if(consoleValuesOn)
      {
        char versionStr[8];
        snprintf(versionStr, sizeof(versionStr), "%d", version);
        Serial.print("Reading version: ");
        Serial.println(versionStr);
      }
      batReadOk |= BATREADOK_VERSION;
    }
    else
    {
      // Error reading Version
      batIDFails++;

      if(consoleValuesOn)
      {
        Serial.println("Reading version: failed");
      }
    }

    // Read Cells
    result = batterybus.requestFrom(batClientID, READHOLDING, BATREG_CELLINFO, BATLEN_CELLINFO);
    if(result > 0) {
      if(result >= BATLEN_CELLINFO)
      {
        for(int i = 0; i < NUMBEROFCELLS; i++)
        {
          int cellVoltage = batterybus.uint16(i);
          float fvoltage = cellVoltage;
          fvoltage /= 1000.0f;
          cellVoltages[i] = fvoltage;
          
          if(consoleValuesOn)
          {
            char voltageStr[12];
            snprintf(voltageStr, sizeof(voltageStr), "%d: %d mV", (i+1), cellVoltage);
            Serial.print("Voltage at cell");
            Serial.println(voltageStr);
          }
        }
        for(int i = 0; i < NUMBEROFTEMPS; i++)
        {
          int cellTemp = batterybus.uint16(i+NUMBEROFCELLS);
          cellTemperatures[i] = cellTemp;
          
          if(consoleValuesOn)
          {
            char TempStr[12];
            snprintf(TempStr, sizeof(TempStr), "%d: %d dC", (i+1), cellTemp);
            Serial.print("Temperture at sensor");
            Serial.println(TempStr);
          }
        }
        batReadOk |= BATREADOK_CELLINFO;
      }
      else
      {
        // Wrong size returned
        if(consoleValuesOn)
        {
          Serial.println("Reading cells: unexpected answer size");
        }
      }
    }
    else
    {
      // Error reading Cells
      batIDFails++;

      if(consoleValuesOn)
      {
        Serial.println("Reading cells: failed");
      }
    }

    // Read Info
    result = batterybus.requestFrom(batClientID, READHOLDING, BATREG_INFO, BATLEN_INFO);
    if(result > 0) {
      if( result >= BATLEN_INFO)
      {
        long value = batterybus.uint16(4);
        float fvalue = value;
        fvalue /= 100.0f;
        battVoltage = fvalue;        
        
        if(consoleValuesOn)
        {
          value *= 10; // Adjust to mV for proper printing
          
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%lu mV", value);
          Serial.print("Voltage: ");
          Serial.println(pStr);

          value = batterybus.uint16(5);
          fvalue = value;
          fvalue /= 10.0f;
          battCurrent = fvalue;
          value *= 100; // Adjust to mA for proper printing
          
          snprintf(pStr, sizeof(pStr), "%ld mA", value);
          Serial.print("Current: ");
          Serial.println(pStr);
        }
        value = batterybus.uint16(8);
        battCurrent = value;
        
        if(consoleValuesOn)
        {
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%ld dC", value);
          Serial.print("BMS Temperature: ");
          Serial.println(pStr);
        }

        value = batterybus.uint16(9);
        battSOC = value;
        
        if(consoleValuesOn)
        {
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%ld %%", value);
          Serial.print("SOC: ");
          Serial.println(pStr);
        }
        //TODO: Read out Battery status and alarms

        batReadOk |= BATREADOK_INFO;
      }
      else
      {
        // Wrong size returned
        if(consoleValuesOn)
        {
          Serial.println("Reading info: unexpected answer size");
        }
      }
    }
    else
    {
      // Error reading Info
      batIDFails++; 

      if(consoleValuesOn)
      {
        Serial.println("Reading info: failed");
      }
    }

    // Read Limits
    result = batterybus.requestFrom(batClientID, READHOLDING, BATREG_LIMITS, BATLEN_LIMITS);
    if(result > 0) {
      if( result >= BATLEN_LIMITS)
      {
        long limit = batterybus.uint16(0);
        float fvalue = limit;
        fvalue /= 100.0f;
        battLimitVoltageCharge = fvalue;

        if(consoleValuesOn)
        {
          limit *= 10; // Adjust to mV for proper printing
          
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%lu mV", limit);
          Serial.print("Voltage limit charge: ");
          Serial.println(pStr);
        }

        limit = batterybus.uint16(1);
        fvalue = limit;
        fvalue /= 100.0f;
        battLimitVoltageDischarge = fvalue;

        if(consoleValuesOn)
        {
          limit *= 10; // Adjust to mV for proper printing
          
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%lu mV", limit);
          Serial.print("Voltage limit discharge: ");
          Serial.println(pStr);
        }

        limit = batterybus.uint16(2);
        fvalue = limit;
        fvalue /= 10.0f;
        battLimitCurrentCharge = fvalue;
        
        if(consoleValuesOn)
        {
          limit *= 100; // Adjust to mA for proper printing
          
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%lu mA", limit);
          Serial.print("Current limit charge: ");
          Serial.println(pStr);
        }

        limit = batterybus.uint16(3);
        fvalue = limit;
        fvalue /= 10.0f;
        battLimitCurrentDischarge = fvalue;
        
        if(consoleValuesOn)
        {
          limit *= 100; // Adjust to mA for proper printing
          
          char pStr[12];
          snprintf(pStr, sizeof(pStr), "%lu mA", limit);
          Serial.print("Current limit discharge: ");
          Serial.println(pStr);
        }

        batReadOk |= BATREADOK_LIMITS;
      }
      else
      {
        // Wrong size returned
        if(consoleValuesOn)
        {
          Serial.println("Reading limits: unexpected answer size");
        }
      }
    }
    else
    {
      // Error reading Limits
      batIDFails++;

      if(consoleValuesOn)
      {
        Serial.println("Reading limits: failed");
      }
    }

    if(batReadOk == BATREADOK_ALL)
    {
      batDataOk = true;
      digitalWrite(LED_BAT_PIN, 1);
    }
    else
    {
      batDataOk = false;
      digitalWrite(LED_BAT_PIN, 0);
    }

    // If too many read fails, try alternate ID
    if(batIDFails > MAXBATIDFAILS)
    {
      if(consoleValuesOn)
      {
        Serial.print("Too many fails, switching ID to ");
        if(batAltIDuse)
        {
          Serial.println(">Default"); //Switch will be made to default
        }
        else
        {
          Serial.println(">Alternate");
        }
      }
      batAltIDuse = !batAltIDuse;
      batIDFails = 0;
    }

    //Check if it makes sense to send data via CAN
    if(batDataOk && canSendMakesSense && (can305Count > 0)) //Wait until we see first x305 frames sent by Victron
    {
      CanFrame txFrame = { 0 };
      bool txResult;

      if(consoleValuesOn)
      {
        Serial.println("Sending frame >Info: ");
      }

      uint8_t BCD_battVersion_High = 0;
      uint8_t BCD_battVersion_Low = 0;
      int battVersionTemp = battVersion;
      if(battVersionTemp < 999)
      {
        while(battVersionTemp >= 100 )
        {
          battVersionTemp -= 100;
          BCD_battVersion_High++;
        }
        BCD_battVersion_Low = (uint8_t)(battVersionTemp & 0xFF);
      }
      txFrame.identifier = 0x35F; // Info
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = 0xAA; // Product code
      txFrame.data[1] = 0x55;
      txFrame.data[2] = BCD_battVersion_High; // Firmware version
      txFrame.data[3] = BCD_battVersion_Low;    
      txFrame.data[4] = 0x64; // Capacity available 100Ah    
      txFrame.data[5] = 0x00;    
      txFrame.data[6] = 0x00;
      txFrame.data[7] = 0x00;
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");      
      }

      if(consoleValuesOn)
      {
        Serial.println("Sending frame >CellInfo: ");
      }

      float minVoltage = 5.0f;
      int minVoltageNum;

      float maxVoltage = 0.0f;
      int maxVoltageNum;

      float minTemp = 200.0f;
      int minTempNum;

      float maxTemp = -200.0f;
      int maxTempNum;

      for(int i = 0; i < NUMBEROFCELLS; i++)
      {
        if(cellVoltages[i] > maxVoltage)
        {
          maxVoltage = cellVoltages[i];
          maxVoltageNum = i;
        }
        if(cellVoltages[i] < minVoltage)
        {
          minVoltage = cellVoltages[i];
          minVoltageNum = i;
        }
      }

      for(int i = 0; i < NUMBEROFTEMPS; i++)
      {
        if(cellTemperatures[i] > maxTemp)
        {
          maxTemp = cellTemperatures[i];
          maxTempNum = i;
        }
        if(cellTemperatures[i] < minTemp)
        {
          minTemp = cellTemperatures[i];
          minTempNum = i;
        }
      }

      maxVoltage *= 1000.0f; // Change to Millivolt
      minVoltage *= 1000.0f;

      minTemp += 273.15f;  // Change to Kelvin
      maxTemp += 273.15f;

      txFrame.identifier = 0x373; // CellInfo
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = (uint8_t)((((int)minVoltage)) & 0xFF); // Lowest Cell Voltage
      txFrame.data[1] = (uint8_t)((((int)minVoltage) >> 8) & 0xFF);
      txFrame.data[2] = (uint8_t)((((int)maxVoltage)) & 0xFF); // Highest Cell Voltage
      txFrame.data[3] = (uint8_t)((((int)maxVoltage) >> 8) & 0xFF);
      txFrame.data[4] = (uint8_t)((((int)minTemp)) & 0xFF); // Minimum Cell Temperature    
      txFrame.data[5] = (uint8_t)((((int)minTemp) >> 8) & 0xFF);
      txFrame.data[6] = (uint8_t)((((int)maxTemp)) & 0xFF); // Maximum Cell Temperature
      txFrame.data[7] = (uint8_t)((((int)maxTemp) >> 8) & 0xFF);
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");      
      }

      if(consoleValuesOn)
      {
        Serial.println("Sending frame >LowCellName: ");
      }

      uint8_t voltageNum10s = 0;
      uint8_t voltageNum1s = 0;

      while((minVoltageNum >= 10) && (minVoltageNum < 100))
      {
        minVoltageNum -= 10;
        voltageNum10s++;
      }
      while((minVoltageNum >= 10) && (minVoltageNum < 100))
      {
        minVoltageNum--;
        voltageNum1s++;
      }

      txFrame.identifier = 0x374; // String to write for the lowest cell
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = 'C';
      txFrame.data[1] = 'e';
      txFrame.data[2] = 'l';
      txFrame.data[3] = 'l';
      txFrame.data[4] = ' ';
      txFrame.data[5] = (voltageNum10s) ? (voltageNum10s + 0x30) : (voltageNum1s + 0x30);
      txFrame.data[6] = (voltageNum10s) ? (voltageNum1s + 0x30) : 0x00;
      txFrame.data[7] = 0x00;
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");
      }

      if(consoleValuesOn)
      {
        Serial.println("Sending frame >HighCellName: ");
      }

      voltageNum10s = 0;
      voltageNum1s = 0;

      while((maxVoltageNum >= 10) && (maxVoltageNum < 100))
      {
        maxVoltageNum -= 10;
        voltageNum10s++;
      }
      while((maxVoltageNum >= 10) && (maxVoltageNum < 100))
      {
        maxVoltageNum--;
        voltageNum1s++;
      }

      txFrame.identifier = 0x375; // String to write for the highest cell
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = 'C';
      txFrame.data[1] = 'e';
      txFrame.data[2] = 'l';
      txFrame.data[3] = 'l';    
      txFrame.data[4] = ' ';    
      txFrame.data[5] = (voltageNum10s) ? (voltageNum10s + 0x30) : (voltageNum1s + 0x30);
      txFrame.data[6] = (voltageNum10s) ? (voltageNum1s + 0x30) : 0x00;
      txFrame.data[7] = 0x00;
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");
      }


      if(consoleValuesOn)
      {
        Serial.println("Sending frame >LowTempName: ");
      }

      txFrame.identifier = 0x376; // String to write for the lowest temp
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = 'S';
      txFrame.data[1] = 'e';
      txFrame.data[2] = 'n';
      txFrame.data[3] = 's';
      txFrame.data[4] = 'o';
      txFrame.data[5] = 'r';
      txFrame.data[6] = ' ';
      txFrame.data[7] = 0x30 + (uint8_t)minTempNum; 
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");
      }

      if(consoleValuesOn)
      {
        Serial.println("Sending frame >LowTempName: ");
      }

      txFrame.identifier = 0x377; // String to write for the highest temp
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = 'S';
      txFrame.data[1] = 'e';
      txFrame.data[2] = 'n';
      txFrame.data[3] = 's';
      txFrame.data[4] = 'o';
      txFrame.data[5] = 'r';
      txFrame.data[6] = ' ';
      txFrame.data[7] = 0x30 + (uint8_t)maxTempNum; 
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");
      }


      if(consoleValuesOn)
      {
        Serial.println("Sending frame >Size: ");
      }

      txFrame.identifier = 0x379; // BatterySize
      txFrame.extd = 0;
      txFrame.data_length_code = 8;
      txFrame.data[0] = 0x64; //100Ah
      txFrame.data[1] = 0x00;
      txFrame.data[2] = 0x00;
      txFrame.data[3] = 0x00;    
      txFrame.data[4] = 0x00;    
      txFrame.data[5] = 0x00;    
      txFrame.data[6] = 0x00;
      txFrame.data[7] = 0x00;
      // Accepts both pointers and references 
      txResult = ESP32Can.writeFrame(txFrame);  // timeout defaults to 1 ms

      if(consoleValuesOn )
      {
        if(txResult)
          Serial.println("ok");
        else
          Serial.println("failed");      
      }
    }

    // Serial.print("Status TXErr: ");
    // Serial.println(ESP32Can.txErrorCounter());
    // Serial.print("Status: ");

    switch(ESP32Can.canState())
    {
      case TWAI_STATE_STOPPED:
      {
        canSendMakesSense = false;
        Serial.println("CAN Status: stopped");
        ESP32Can.restart();
        Serial.println("CAN Command: begin");      
        break;
      }
      case TWAI_STATE_RUNNING:
      {
        canSendMakesSense = true;
        break;
      }
      case TWAI_STATE_BUS_OFF:
      {
        canSendMakesSense = false;
        Serial.println("CAN Status: Busoff");
        ESP32Can.recover();
        Serial.println("CAN Command: recover");
        break;
      }
      case TWAI_STATE_RECOVERING:
      {
        canSendMakesSense = false;
        break;
      }
      default:
      {
        canSendMakesSense = false;
        Serial.println("CAN Status: unknown");
        break;
      }
    }
  }

  delay(1);
}

/******************************/
/* User Functions             */
/******************************/

// void onCANReceive(int packetSize) {
//   // received a packet
//   Serial.print("Received ");

//   if (CAN.packetExtended()) {
//     Serial.print("extended ");
//   }

//   if (CAN.packetRtr()) {
//     // Remote transmission request, packet contains no data
//     Serial.print("RTR ");
//   }

//   Serial.print("packet with id 0x");
//   Serial.print(CAN.packetId(), HEX);

//   if (CAN.packetRtr()) {
//     Serial.print(" and requested length ");
//     Serial.println(CAN.packetDlc());
//   } else {
//     Serial.print(" and length ");
//     Serial.println(packetSize);

//     // only print packet data for non-RTR packets
//     while (CAN.available()) {
//       Serial.print((char)CAN.read());
//     }
//     Serial.println();
//   }

//   Serial.println();
// }

