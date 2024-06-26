# FelicityConverter
An ESP32 based monitor/gateway for LPBA48100-OL Felicity Home Storage Batteries. Reads out all battery data via RS485 and can act as a e.g. MQTT Client sending them to some home-automation (TBD). Works as gateway and sends the data not being sent by battery (e.g. highest cell voltage) to Victron GX devices via BMS-Can.

## Pinout PCS connector at battery
| Pin number | Role    | Remarks                                                        |
|------------|---------|----------------------------------------------------------------|
| 1          | GND     |                                                                |
| 2          |         |                                                                |
| 3          | NC?     | Unsure if it has internal conenction. Better leave unconnected |
| 4          | NC?     | Unsure if it has internal conenction. Better leave unconnected |
| 5          | RS485-B |                                                                |
| 6          | RS485-A |                                                                |
| 7          | CANL    |                                                                |
| 8          | CANH    |                                                                |

## Pinout RS485 connector at battery
| Pin number | Role    | Remarks                                                        |
|------------|---------|----------------------------------------------------------------|
| 1          | GND     |                                                                |
| 2          | 12V     | 12V supply is available when battery is on, capability unknown |
| 3          | NC      |                                                                |
| 4          | NC      |                                                                |
| 5          | RS485-B |                                                                |
| 6          | RS485-A |                                                                |
| 7          | NC      |                                                                |
| 8          | NC      |                                                                |

## RS485 / Modbus
Baudrate is 9600Bd. Battery seems to implement Modbus (or some sort of, didn't check), at least one can talk to it using Modbus-Libs.

## CAN
The batteries that talk already CAN only send some frames. Baudrate is 500kBd. 
The frames are always sent multiple times (e.g. five consecutive frames of the same content), maybe they hope that at least one makes it to the Victron.
Battery sends an Error-Frame after each 4th x305/x307-Burst by Victron. It seams to have problems processing the x307-Frame by the Victron. Luckily with the gateway, this behavior vanishes as the gateway sends an ACK to the x307-Frame before the battery can drop off the Error-Frame.

### Frames sent by battery
| CAN ID | Content        | Remarks                                                                                                |
|--------|----------------|--------------------------------------------------------------------------------------------------------|
| 0x359  | Alarms         |                                                                                                        |
| 0x351  | DVCC           | Contains limits of battery (Max. voltage, min. voltage, charge current limit, discharge current limit) |
| 0x355  | State          |                                                                                                        |
| 0x356  | Stats          |                                                                                                        |
| 0x35C  | Charge Request |                                                                                                        |
| 0x35E  | Name           | "PYLON"                                                                                                |

