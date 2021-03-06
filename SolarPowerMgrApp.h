#ifndef SOLAR_POWER_MGR_APP_H
#define SOLAR_POWER_MGR_APP_H

#include "automation/Automation.h"
#include "automation/constraint/ConstraintEventHandler.h"
#include "automation/device/Device.h"
#include "automation/sensor/Sensor.h"

#include <Poco/Util/Application.h>

using namespace std;
using namespace automation;
using namespace Poco;

#define DEFAULT_APPLIANCE_WATTS 465 // air conditioner
//#define DEFAULT_APPLIANCE_WATTS 520 // heater

#define DEFAULT_MIN_VOLTS 24.70

#define LIGHTS_SET_1_WATTS 120
#define LIGHTS_SET_2_WATTS 160

#define FULL_SOC_PERCENT 98.00


class SolarPowerMgrApp : public Poco::Util::Application, ConstraintEventHandler {


public:
  void resultDeferred(Constraint* pConstraint,bool bNext,unsigned long delayMs) const override {
    //logBuffer << "DEFERRED(next=" << bNext << ",delay=" << delayMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };
  void resultChanged(Constraint* pConstraint,bool bNew,unsigned long lastDurationMs) const override {
    logBuffer << "CHANGED(new=" << bNew << ",lastDuration=" << lastDurationMs/1000.0 << "s): ";
    // Print device name if not current device
    if ( pConstraint && currentDevice && !currentDevice->findConstraint(pConstraint->id) ) {
      Device* pDevice = devices.findConstraintOwner(pConstraint->id);
      if ( pDevice ) {
        logBuffer << "DEVICE: '" << pDevice->getTitle() << "' CONSTRAINT: ";
      }
    }
    printConstraintTitleAndValue(logBuffer,pConstraint);
    logBuffer << endl;
    //logBuffer << pConstraint->getTitle() << endl;
  };
  //void resultSame(Constraint* pConstraint,bool bVal,unsigned long lastDurationMs) const override {
  //  logBuffer << "SAME(value=" << bVal << ",duration=" << lastDurationMs << "ms): " << pConstraint->getTitle() << endl;
  //};
  void deferralCancelled(Constraint* pConstraint,bool bCurrent,unsigned long lastDurationMs) const override {
    //logBuffer << "DEFERRAL CANCELED(current=" << bCurrent << ",duration=" << lastDurationMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };

  static SolarPowerMgrApp* pInstance;
  static bool iSignalCaught;
  static void signalHandlerFn (int val);

  Devices devices;
  Sensors sensors;
  automation::Device* currentDevice = nullptr;

  bool bEnabled;

  SolarPowerMgrApp() : bEnabled(true) {
    pInstance = this;
  }

  virtual int main(const std::vector<std::string> &args);

  protected:
  
  std::ostream& printConstraintTitleAndValue(std::ostream& os, Constraint* pConstraint) const;
  
};

#endif