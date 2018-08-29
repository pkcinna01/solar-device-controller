#ifndef AUTOMATION_POWERSWITCH_H
#define AUTOMATION_POWERSWITCH_H

#include "Device.h"
#include "../capability/Toggle.h"

#include <functional>

using namespace std;

namespace automation {
    class PowerSwitch : public Device {

    public:

      Toggle toggle;

      PowerSwitch(const string &id) : Device(id) {

          toggle.readHandler = std::bind(&PowerSwitch::isOn,this);
          toggle.writeHandler = std::bind(&PowerSwitch::setOn,this,std::placeholders::_1);
          capabilities.push_back(&toggle);
      }

      virtual bool isOn() = 0;
      virtual void setOn(bool bOn) = 0;

      virtual void constraintResultChanged(bool bConstraintResult) {
        toggle.setValue(bConstraintResult);
      }
    };

}
#endif //AUTOMATION_POWERSWITCH_H
