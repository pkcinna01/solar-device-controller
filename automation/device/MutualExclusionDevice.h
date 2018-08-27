#ifndef AUTOMATION_MUTUALEXCLUSION_DEVICE_H
#define AUTOMATION_MUTUALEXCLUSION_DEVICE_H

#include "Device.h"
#include "../constraint/Constraints.h"

using namespace std;

namespace automation {
  class MutualExclusionDevice : public Device {

  public:

    vector<Device *> devices;

    long maxSelectedDurationMs;
    unsigned int selectedIndex = 0;
    unsigned long selectTimeMs = automation::millisecs();

    MutualExclusionDevice(const string &id, const vector<Device *> &devices, long maxSelectedDurationMs = -1) :
      Device(id),
      devices(devices),
      maxSelectedDurationMs(maxSelectedDurationMs){
    }

    virtual Device* updateSelectedDevice() {
      if (selectedExpired() || !testSelected() ) {
        setSelectedIndex(selectedIndex + 1);
      }
      return getSelected();
    }

    virtual Device* getSelected() {
      return devices.empty() ? nullptr : devices[selectedIndex];
    }

    virtual bool testSelected() {
      return devices.empty() ? false : devices[selectedIndex]->testConstraint();
    }

    virtual void setSelectedIndex(int index) {
      if (index >= devices.size()) {
        index = 0;
      }
      if ( index != selectedIndex ) {
        selectTimeMs = automation::millisecs();
      }
    }

    virtual bool selectedExpired() {
      unsigned long now = automation::millisecs();
      unsigned long duration = now - selectTimeMs;
      if ( duration > maxSelectedDurationMs ) {
        cout << "Max duration exceeded for selected mutual exclusion item" << endl;
        return true;
      }
      return false;
    }

    void applyConstraints(bool bIgnoreSameState = true,Constraint* pConstraint = nullptr) override {
      Device* pLastSelectedDevice = getSelected();
      Device* pSelectedDevice = updateSelectedDevice();
      if ( pLastSelectedDevice != pSelectedDevice ) {
        pLastSelectedDevice->applyConstraints(true,&automation::FAIL_CONSTRAINT);
      }
      if (pSelectedDevice) {
        pSelectedDevice->applyConstraints(bIgnoreSameState);
      }
    }


    virtual void constraintsResultChanged(bool bConstraintResult) {
      Device* pSelectedDevice = getSelected();
      if (pSelectedDevice) {
        pSelectedDevice->constraintsResultChanged(bConstraintResult);
      }
    }
  };

}
#endif //AUTOMATION_POWERSWITCH_H
