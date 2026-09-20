# Clarinet Hygrothermometer
<table> <tr> <td><img src="https://github.com/user-attachments/assets/32310b38-0417-4cc2-9e28-586226fb1749" alt="My Clarinets" width="400"/></td> <td><img src="https://github.com/user-attachments/assets/88515d74-24d2-487d-8d6f-b6765d1ee2e2" alt="Functioning PCB" width="400"/></td> </tr> </table>

## Purpose & Motivation:
Having played the clarinet for over 8 years, I've grown to love my wooden instrument. However, in February of 2026, my instrument cracked due to a sudden drop in humidity (as my technician pointed out that wintertime brings dryer air). Thus, to prevent future issues and to save money from expensive repairs, I designed a PCB to accurately monitor the humidity and temperature within my clarinet case.

## Design

### Component List (BOM)
1. ESP32-C3-WROOM-02 ([Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3-wroom-02_datasheet_en.pdf))
2. MCP73831-2-OT Charge Controller ([Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/20001984g.pdf))
3. AP2112K-3.3 LDO Voltage Regulator ([Datasheet](https://www.diodes.com/assets/Datasheets/AP2112.pdf))
4. GCT USB4105-GF-A USB-C Receptacle 16P SMD RA ([Datasheet](https://gct.co/files/drawings/usb4105.pdf))
5. SHT31 Humidity & Temperature Sensor ([Datasheet](https://sensirion.com/media/documents/21373A00/6164175B/Sensirion_Humidity_Sensors_SHT3x_Datasheet_digital.pdf))
6. 3.7V 1100mAh (4.1Wh) Lithium-Polymer Battery
7. OLED Display Breakout Board (4-pin I2C)
8. SHT31 Breakout Board (4-pin I2C)
9. Passives & Miscellaneous Hardware:
   - Capacitors (0805, Ceramic)
   - Resistors (0805)
   - Charing Indicator LED
   - Push Buttons


### PCB, Schematic, 3D Render
The Schematic and PCB were designed in KiCad.

<img src="https://github.com/user-attachments/assets/faca2936-ddf8-464d-a931-f0d31780c822" alt="Schematic" width="100%"/>

<table width="100%">
  <tr>
    <td width="50%"><img src="https://github.com/user-attachments/assets/1c6d121b-9e13-471a-b5f9-7dac0a3db839" alt="" width="100%"/></td>
    <td width="50%"><img src="https://github.com/user-attachments/assets/dfbfa52e-25d6-4326-9c1f-8ae7927f5973" alt="" width</tr>
</table>


### Measurements

 I tested the accuracy of the SHT31 by putting the board in an enclosed tupperware container with a 49% Boveda (c) humidity pack, and with a small thermometer. I found the following:
* **Relative Humidity:** Accurate to ±2%.
* **Temperature:** Consistently reads ~0.8°C high. I believe this exceeds datasheet tolerances due to the sensor picking up some heat dissipated by the ESP32 (due to their proximity).


### Firmware
* **Language & Framework:** C / ESP-IDF, done using VSCode with PlatformIO 
* **I2C Configuration:** GPIO 0 (SDA), GPIO 1 (SCL) at 100 kHz
* **Sampling Rate:** 2 Hz FreeRTOS task loop
* **Display Driver:** Direct 1024-byte screen buffer with custom 5x7 ASCII font rendering (no third-party libraries used)
## Assembly & Demonstration
### Assembly & Demonstration

**Short Demo**  
<video src="https://github.com/user-attachments/assets/3b9bc241-1b57-4820-86a4-abee198bdf83" width="100%" controls></video>

* **Assembly:** Hand-soldered all SMD parts and tested the board with a multimeter (checking all voltage readings: VBAT, 3v3, 5V and ensuring no shorts) before flashing firmware.

## Active Development

* **Power Optimization:** Moving to deep-sleep mode to extend battery life beyond the current ~30-hour calculated runtime.
* **Altium Migration:** Rebuilding the PCB in Altium to gain experience with the software for future projects.
* **Data Logging:** Adding BLE (Bluetooth Low Energy) services to stream the SHT31 readings to my phone to monitor changes over time (i.e. how does the humidity change depending on the season?).


<div align="center">
  <p><strong>My messy home Workbench</strong></p>
  <img src="https://github.com/user-attachments/assets/867cf211-ea1a-448b-9fba-3c455d99b579" alt="Workbench" width="400"/>
</div>
