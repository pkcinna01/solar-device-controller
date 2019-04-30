#ifndef XMONIT_DEVICE_CONTROLLER_H
#define XMONIT_DEVICE_CONTROLLER_H

#include "automation/Automation.h"
#include "automation/constraint/ConstraintEventHandler.h"
#include "automation/device/Device.h"

#include <Poco/Util/Application.h>

using namespace std;
using namespace automation;
using namespace Poco;

// Summer window AC units use 515 (AC + Fan)
// Winter heaters use 575 watts
#define DEFAULT_APPLIANCE_WATTS 515

#define DEFAULT_MIN_VOLTS 24.65

#define LIGHTS_SET_1_WATTS 170
#define LIGHTS_SET_2_WATTS 100

#define MIN_SOC_PERCENT 48.25
#define FULL_SOC_PERCENT 99.00


class DeviceControllerApp : public Poco::Util::Application, ConstraintEventHandler {


public:
  void resultDeferred(Constraint* pConstraint,bool bNext,unsigned long delayMs) const override {
    logBuffer << "DEFERRED(next=" << bNext << ",delay=" << delayMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };
  void resultChanged(Constraint* pConstraint,bool bNew,unsigned long lastDurationMs) const override {
    logBuffer << "CHANGED(new=" << bNew << ",lastDuration=" << lastDurationMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };
  //void resultSame(Constraint* pConstraint,bool bVal,unsigned long lastDurationMs) const override {
  //  logBuffer << "SAME(value=" << bVal << ",duration=" << lastDurationMs << "ms): " << pConstraint->getTitle() << endl;
  //};
  void deferralCancelled(Constraint* pConstraint,bool bCurrent,unsigned long lastDurationMs) const override {
    logBuffer << "DEFERRAL CANCELED(current=" << bCurrent << ",duration=" << lastDurationMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };

  static DeviceControllerApp* pInstance;
  static bool iSignalCaught;
  static void signalHandlerFn (int val);

  Devices devices;
  automation::Device* currentDevice = nullptr;

  DeviceControllerApp(){
    pInstance = this;
  }

  virtual int main(const std::vector<std::string> &args);
  
};

#endif