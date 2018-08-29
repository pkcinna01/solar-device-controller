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

    MutualExclusionDevice(const string &id, const vector<Device *> &devices, long maxSelectedDurationMs = 30*60000) :
      Device(id),
      devices(devices),
      maxSelectedDurationMs(maxSelectedDurationMs){
    }

    void applyConstraint(bool bIgnoreSameState, Constraint *pConstraint) override {
      //Device::applyConstraint(bIgnoreSameState, pConstraint);
      Device* pLastSelectedDevice = getSelected();
      Device* pSelectedDevice = updateSelectedDevice();
      if ( pLastSelectedDevice != pSelectedDevice ) {
        pLastSelectedDevice->applyConstraint(true, &automation::FAIL_CONSTRAINT);
      }
      if (pSelectedDevice) {
        pSelectedDevice->applyConstraint(bIgnoreSameState, pConstraint);
      }
    }

    void constraintResultChanged(bool bConstraintResult) override {
      cout << __PRETTY_FUNCTION__ << " unsupported.  Trying to set bConstrainResult: " << bConstraintResult << endl;
    }

    virtual bool testConstraint() {
      cout << __PRETTY_FUNCTION__ << endl;
      Device* pDevice = getSelected();
      return pDevice ? pDevice->testConstraint() : false;
    }

    virtual Device* updateSelectedDevice() {
      if ( selectedExpired() ) {
        setSelectedIndex(selectedIndex + 1);
      }
      // test all devices to update constraint states
      int bSelectedIndexChanged = false;
      for( int i = 0; i < devices.size(); i++ ){
        int index = (selectedIndex+i)%devices.size();
        if ( devices[index]->testConstraint() && index != selectedIndex) {
          if ( !bSelectedIndexChanged ) {
            bSelectedIndexChanged = true;
            setSelectedIndex(index);
          }
        }
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
        cout << __PRETTY_FUNCTION__ << " old:" << selectedIndex << ", new:" << index << endl;
        selectTimeMs = automation::millisecs();
        //if ( pConstraint ) {
        //  pConstraint->resetDeferredState();
        //}
      }
      selectedIndex = index;
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
  };

}
#endif //AUTOMATION_POWERSWITCH_H
