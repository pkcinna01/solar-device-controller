#ifndef AUTOMATION_POWERSWITCH_H
#define AUTOMATION_POWERSWITCH_H

#include "Device.h"
#include "../capability/Toggle.h"

#include <functional>

using namespace std;

namespace automation {
    class PowerSwitch : public Device {

    public:

      struct PowerSwitchToggle : automation::Toggle {
        PowerSwitchToggle(Device* parent) : Toggle(parent){};
        PowerSwitch* pPowerSwitch;
        double getValueImpl() const override { return (double) pPowerSwitch->isOn(); }
        void setValueImpl(double val) override { pPowerSwitch->setOn(val!=0); }
      } toggle;

      PowerSwitch(const string &id) : Device(id), toggle(this) {
          toggle.pPowerSwitch = this;
          capabilities.push_back(&toggle);
      }

      virtual bool isOn() const = 0;
      virtual void setOn(bool bOn) = 0;

      virtual void constraintResultChanged(bool bConstraintResult) {
        //cout << __PRETTY_FUNCTION__ << " bConstraintResult: " << bConstraintResult << endl;
        toggle.setValue(bConstraintResult);
      }
    };

}
#endif //AUTOMATION_POWERSWITCH_H
