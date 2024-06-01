# Project-Carl

## Summary
**Project Carl** is name after a Swedish biologist and physician *Carl Linnaeus*, also known after ennoblement in 1761 as Carl von Linné man is widely acknowledged as the **“Father of Modern Botany"**

This project addresses the Plant health monitoring need using a IOT based product. Scope of this project is limited by the hardware and more focused on to the Firmware of the hardware.

The system is shown below. TBD - more details

![Block Diagram](./documents/media/Project_Carl_System_Overview.png "System Overview")

### Hardware used

- [Nordic Thingy:53](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-53)
    - [BME688](https://cdn.shopify.com/s/files/1/0174/1800/files/bst-bme688-ds000.pdf?v=1620834794) | Digital low power gas, pressure, temperature & humidity sensor


##  Building & Running

### Windows 11 (Home Edition)

#### Install module
* [nRF SDK 2.5.99-dev](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.5.99-dev1/nrf/installation.html)
1. Clone this repository:
 ```
$ git clone https://github.com/manu897/Project-Carl.git
```
2. Open project in VSCode
3. Select nRF Connect on VSCode
4. Build the application for board
```
$ thingy53_nrf5340_cpuapp_ns
```
5. Flash the build on to the Thingy:53, click [here](https://academy.nordicsemi.com/flash-instructions-for-the-thingy53/) for instructions.
6. Open COM port under 115200 baud rate, putty.

## Code file Structure

```
* build
* src
    |__ main.c
            |__ bme688_interface.c
            |__ bme688_reg.h
            |__ ble.c
            |__ ble.h
* CMakeLists.txt
* prj.conf
* thingy53_nrf5340_cpuapp_ns.overlay
```

# Build & Test Status

Windows 11 home edition

![Static Badge](https://img.shields.io/badge/build-TB_tested-orange)

Linux(Ubuntu 22.0.2)

![Static Badge](https://img.shields.io/badge/build-unknown-white)

MacOs Sonoma 14.5 (23F79)

![Static Badge](https://img.shields.io/badge/build-Failed-red)

## Author

[Manideep Reddy Tamma](mailto:manideep@bioliberty.co.uk) | [LinkedIn](https://www.linkedin.com/in/manideep-reddy-tamma/)

## Reference

* [nRF Connect SDK Fundamentals](https://academy.nordicsemi.com/courses/nrf-connect-sdk-fundamentals/)
* [Nordic Thingy: 53 Datasheet](https://infocenter.nordicsemi.com/pdf/Thingy53_UG.pdf)
* [BME688 DataSheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme688-ds000.pdf)
* [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop)
* [putty](https://www.putty.org/)
* [Soil Moisture selection](https://metergroup.com/measurement-insights/soil-moisture-sensors-how-they-work-why-some-are-not-research-grade/)