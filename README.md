# solar-ifttt (Solar OFF Grid management of appliances)
## Summary
C++ console app designed to run as a service.  It manages devices and power switches powered by a small solar setup. It uses data from solar equipment and current and voltage sensors exported via Prometheus to determine best use of HVAC equipment and plant lights.  The automation folder is shared with an Arduino project.

### Prerequisites
1) Raspberry PI 3b+ and OpenHab with CC2531 Zigbee Coordinator (USB dongle)
2) C++ CMake and local POCO build
3) Prometheus c++ client and its dependencies (https://github.com/jupp0r/prometheus-cpp)

