#ifndef XMONIT_GPIOPOWERSWITCH_H
#define XMONIT_GPIOPOWERSWITCH_H

#include "../automation/device/PowerSwitch.h"
#include "xmonit.h"
#include <sstream>

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

// simple Raspberry PI pin on/off implementation 
// For external relays or powerstrips that just need a power signal
namespace xmonit {


  int exec(const std::string& cmd, std::string& strOutput) {
    std::array<char, 128> buffer;
    strOutput.clear();
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {      
        return -1;
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        strOutput += buffer.data();
    }
    return 0;
  }

  class GpioPowerSwitch : public automation::PowerSwitch {
  public:
    RTTI_GET_TYPE_IMPL(xmonit,GpioPowerSwitch);

    int gpioPin;

    GpioPowerSwitch(const string &id, int gpioPin, float requiredWatts = 0) : automation::PowerSwitch(id,requiredWatts), gpioPin(gpioPin) {}

    void setup() override {
      std::stringstream cmdStream;
      cmdStream << "gpio mode " << gpioPin << " out";
      std::string response;
      int rtn = xmonit::exec(cmdStream.str(),response);
      automation::logBuffer << __PRETTY_FUNCTION__ << " cmd='" << cmdStream.str() << "' rtn=" << rtn << endl;
      bError = rtn != 0;
    }

    bool isOn() const override {
      std::stringstream cmdStream;
      cmdStream << "gpio read " << gpioPin;
      std::string response;
      int rtn = xmonit::exec(cmdStream.str(),response);
      automation::text::rtrim(response);
      //automation::logBuffer << __PRETTY_FUNCTION__ << " cmd='" << cmdStream.str() << "' response='" << response << "'"<< endl;
      bError = rtn != 0;
      return !bError && response == "1";
    }

    void setOn(bool bOn) override {
      std::stringstream cmdStream;
      cmdStream << "gpio write " << gpioPin << " " << (bOn?"1":"0");
      std::string response;
      int rtn = xmonit::exec(cmdStream.str(),response);
      automation::logBuffer << __PRETTY_FUNCTION__ << " cmd='" << cmdStream.str() << "' rtn=" << rtn << endl;
      bError = rtn != 0;
    }
  };
}
#endif //SOLAR_IFTTT_TOGGLE_H
