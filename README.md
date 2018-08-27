# solar-ifttt (Solar OFF Grid management of appliances)
## Summary
High level way to control smart power plugs with free IFTTT service.  This utility uses a Samsung SmartThings hub to control 3 smart switches.  It gathers metrics from a Prometheus server and uses rules (Constraints) to determine the best usage of air conditioners based on the amount of solar power available.  Some important metrics are currently not available in Prometheus such as power consumption of each outlet due to some hardware issues.  When those power details are back, this utility should have very good control of managing appliances with a small solar setup. Right now it makes some assumptions based on battery bank state and change controller output.

### Prerequisites
1) Uses IFTTT account and the webhooks service. Requires creating webhook custom applets from the IFTTT developer site.
2) C++ CMake and local POCO build

### Conclusion
This utility proves it is possible to manage SmartThings hub devices from IFTTT.  The main limitation of this approach is IFTTT does not have a way to query devices and get their state.  The SmartThings API in IFTTT only allows things like turning switches on and off.  It is not a good long term automation approach because it requires an external internet connection with lots of chatter to do simple things (This C++ app --> IFTTT remote web service --> Samsung SmartThings remote web service --> Local SmartHub --> Local Zigbee or ZWave power switch).  
