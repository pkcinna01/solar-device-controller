
#include "ifttt/WebHookSession.h"
#include "ifttt/WebHookEvent.h"
#include "ifttt/PowerSwitch.h"
#include "xmonit/PowerSwitch.h"
#include "automation/Automation.h"
#include "automation/json/JsonStreamWriter.h"
#include "automation/constraint/ConstraintEventHandler.h"
#include "automation/constraint/NotConstraint.h"
#include "automation/constraint/AndConstraint.h"
#include "automation/constraint/OrConstraint.h"
#include "automation/constraint/BooleanConstraint.h"
#include "automation/constraint/ValueConstraint.h"
#include "automation/constraint/ToggleConstraint.h"
#include "automation/constraint/SimultaneousConstraint.h"
#include "automation/constraint/TimeRangeConstraint.h"
#include "automation/constraint/TransitionDurationConstraint.h"
#include "automation/device/Device.h"

#include "Prometheus.h"

#include "Poco/Util/Application.h"
#include <signal.h>
#include <iostream>
#include <numeric>

using namespace std;
using namespace automation;
using namespace ifttt;


// Summer window AC units use 525 (AC + Fan)
// Winter heaters use 575 watts
#define DEFAULT_APPLIANCE_WATTS 550

#define DEFAULT_MIN_VOLTS 24.70

#define LIGHTS_SET_1_WATTS 100
#define LIGHTS_SET_2_WATTS 100

#define MIN_SOC_PERCENT 48.00

bool iSignalCaught = 0;
static void signalHandlerFn (int val) { iSignalCaught = val; }

class LogConstraintEventHandler : public ConstraintEventHandler{
  public:
  void resultDeferred(Constraint* pConstraint,bool bNew,unsigned long delayMs) const override {
    logBuffer << "DEFERRED(next=" << bNew << ",delay=" << delayMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
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
} logConstraintEventHandler;
  


class IftttApp : public Poco::Util::Application {

public:
  virtual int main(const std::vector<std::string> &args) {

    ConstraintEventHandlerList::instance.push_back(&logConstraintEventHandler);
    cout << "START TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;

    auto metricFilter = [](const Prometheus::Metric &metric) { return metric.name.find("solar") == 0; };

    static Prometheus::DataSource prometheusDs(Prometheus::URL, metricFilter);

    static SensorFn soc("State of Charge", []()->float{ return prometheusDs.metrics["solar_charger_batterySOC"].avg(); });
    static SensorFn chargersOutputPower("Chargers Output Power",
                                 []()->float{ return prometheusDs.metrics["solar_charger_outputPower"].total(); });
    static SensorFn chargersInputPower("Chargers Input Power",
                                   []()->float{ return prometheusDs.metrics["solar_charger_inputPower"].total(); });
    static SensorFn batteryBankVoltage("Battery Bank Voltage",
                                     []()->float{ return prometheusDs.metrics["solar_charger_outputVoltage"].avg(); });

    static vector<automation::PowerSwitch *> devices;
    static automation::PowerSwitch* currentDevice = nullptr;

    static SensorFn requiredPowerTotal("Required Power", []()->float{ 
      float rtnWatts = 0;
      //cout << "Begin requiredPowerTotal() count:" << devices.size() << endl;
      for( automation::PowerSwitch *pDevice : devices ) {
        //cout << "Device '" << pDevice->name << "' ON: " << pDevice->isOn() << ", Current: " << (pDevice == currentDevice) << endl;
        if ( pDevice->isOn() || pDevice == currentDevice ) {
          rtnWatts += pDevice->requiredWatts;
        }
      }
      //cout << "End requiredPowerTotal() = " << rtnWatts << endl;
      return rtnWatts;
    });

    static struct FamilyRoomMasterSwitch : ifttt::PowerSwitch {

      AtLeast<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      AtLeast<float,Sensor&> cutoffVoltage {22, batteryBankVoltage};
      AtLeast<float,Sensor&> minSoc {MIN_SOC_PERCENT, soc};
      AtLeast<float,Sensor&> haveRequiredPower{requiredPowerTotal, chargersInputPower};
      AndConstraint enoughPower {{&minSoc, &haveRequiredPower}};
      AtLeast<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {10,00,0},{15,30,00} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{4*MINUTES,&toggle,0,1};
      AndConstraint familyRmMasterConstraints {{&timeRange,&notSimultaneousToggleOn,&minVoltage,&cutoffVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      FamilyRoomMasterSwitch() :
          ifttt::PowerSwitch("Family Room Master",DEFAULT_APPLIANCE_WATTS) {
        setOnEventLabel("family_room_master_switch_on");
        setOffEventLabel("family_room_master_switch_off");
        fullSoc.setPassDelayMs(30*SECONDS).setFailDelayMs(90*SECONDS).setFailMargin(25);
        minSoc.setPassDelayMs(1*MINUTES).setFailDelayMs(120*SECONDS).setFailMargin(20).setPassMargin(5);
        haveRequiredPower.setPassDelayMs(30*SECONDS).setFailDelayMs(120*SECONDS).setFailMargin(125).setPassMargin(100);
        minVoltage.setFailDelayMs(30*SECONDS).setFailMargin(0.5);
        pConstraint = &familyRmMasterConstraints;
      }
    } familyRoomMasterSwitch;

    static struct SunroomMasterSwitch : ifttt::PowerSwitch {

      AtLeast<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      AtLeast<float,Sensor&> minSoc {MIN_SOC_PERCENT, soc};
      AtLeast<float,Sensor&> cutoffVoltage {22.75, batteryBankVoltage};
      AtLeast<float,Sensor&> haveRequiredPower{requiredPowerTotal, chargersInputPower};
      AndConstraint enoughPower {{&minSoc, &haveRequiredPower}};
      AtLeast<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {8,30,0},{15,30,00} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{4*MINUTES,&toggle,0,1};
      AndConstraint sunroomMasterConstraints {{&timeRange,&notSimultaneousToggleOn,&minVoltage,&cutoffVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      SunroomMasterSwitch() :
          ifttt::PowerSwitch("Sunroom Master",DEFAULT_APPLIANCE_WATTS) {
        setOnEventLabel("sunroom_master_switch_on");
        setOffEventLabel("sunroom_master_switch_off");
        fullSoc.setPassDelayMs(30*SECONDS).setFailDelayMs(120*SECONDS).setFailMargin(25);
        minSoc.setPassDelayMs(2*MINUTES).setFailDelayMs(45*SECONDS).setFailMargin(20).setPassMargin(10);
        haveRequiredPower.setPassDelayMs(30*SECONDS).setFailDelayMs(45*SECONDS).setFailMargin(125).setPassMargin(100);
        minVoltage.setFailDelayMs(60*SECONDS).setFailMargin(0.5);
        pConstraint = &sunroomMasterConstraints;
      }
    } sunroomMasterSwitch;

    static struct FamilyRoomAuxSwitch : ifttt::PowerSwitch {

      AtLeast<float,Sensor&> minSoc {MIN_SOC_PERCENT, soc};
      AtLeast<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      AtLeast<float,Sensor&> haveRequiredPower{requiredPowerTotal, chargersInputPower};
      AndConstraint enoughPower {{&minSoc, &haveRequiredPower}};
      AtLeast<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {8,30,0},{15,30,0} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{4*MINUTES,&toggle,0,1};
      AndConstraint familyRmAuxConstraints {{&timeRange,/*&allMastersMustBeOn,*/
        &notSimultaneousToggleOn,&minVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      FamilyRoomAuxSwitch() :
          ifttt::PowerSwitch("Family Room Auxiliary",LIGHTS_SET_1_WATTS) {
        setOnEventLabel("family_room_aux_switch_on");
        setOffEventLabel("family_room_aux_switch_off");
        fullSoc.setPassDelayMs(1*MINUTES).setFailDelayMs(30*SECONDS).setFailMargin(15);
        minSoc.setPassDelayMs(3*MINUTES).setFailDelayMs(45*SECONDS).setFailMargin(25);
        haveRequiredPower.setPassDelayMs(30*SECONDS).setFailDelayMs(5*MINUTES).setFailMargin(5).setPassMargin(25);
        minVoltage.setFailDelayMs(60*SECONDS).setFailMargin(0.5);
        pConstraint = &familyRmAuxConstraints;
      }
    } familyRoomAuxSwitch;
    
    /*
    static struct Outlet1Switch : xmonit::PowerSwitch {

      AtLeast<float,Sensor&> minSoc {MIN_SOC_PERCENT, soc};
      AtLeast<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      AtLeast<float,Sensor&> haveRequiredPower{requiredPowerTotal, chargersInputPower};
      AndConstraint enoughPower {{&minSoc, &haveRequiredPower}};
      AtLeast<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {8,30,0},{15,30,0} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{4*MINUTES,&toggle,0,1};
      AndConstraint allConstraints {{&timeRange,&notSimultaneousToggleOn,&minVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      Outlet1Switch() :
          //IMPORTANT: PowerSwitch ID in constructor must match a name of switch defined in the remote ATMega
          xmonit::PowerSwitch("Sunroom Outlet 1",LIGHTS_SET_2_WATTS) {
        fullSoc.setPassDelayMs(1*MINUTES).setFailDelayMs(30*SECONDS).setFailMargin(15);
        minSoc.setPassDelayMs(3*MINUTES).setFailDelayMs(45*SECONDS).setFailMargin(25);
        haveRequiredPower.setPassDelayMs(30*SECONDS).setFailDelayMs(5*MINUTES).setFailMargin(5).setPassMargin(25);
        minVoltage.setFailDelayMs(60*SECONDS).setFailMargin(0.5);
        pConstraint = &allConstraints;
      }
    } outlet1Switch;
    */

    devices = {&familyRoomMasterSwitch, &sunroomMasterSwitch, &familyRoomAuxSwitch}; //, &outlet1Switch};

    SimultaneousConstraint::connectListeners({&familyRoomMasterSwitch.simultaneousToggleOn, &familyRoomAuxSwitch.simultaneousToggleOn, 
      &sunroomMasterSwitch.simultaneousToggleOn/*, &outlet1Switch.simultaneousToggleOn*/});    

    unsigned long nowMs = automation::millisecs();
    unsigned long syncTimeMs = nowMs;

    bool bFirstTime = true;

    TimeRangeConstraint solarTimeRange({0,0,0},{16,00,0}); // app exits at 4:00pm each day (in winter)
    
    struct sigaction action;
    action.sa_handler = signalHandlerFn;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);

    while ( solarTimeRange.test() && iSignalCaught==0) {

      int iDeviceErrorCnt = 0;

      for (automation::PowerSwitch *pPowerSwitch : devices) {

        prometheusDs.loadMetrics();

        currentDevice = pPowerSwitch;
        automation::clearLogBuffer();
        bool bIgnoreSameState = !bFirstTime;
        pPowerSwitch->applyConstraint(bIgnoreSameState);
        if ( pPowerSwitch->bError ) {
          iDeviceErrorCnt++;
        }
        string strLogBuffer;
        automation::logBufferToString(strLogBuffer);
        if ( !strLogBuffer.empty() ) {
          cout << "=================================================================================" << endl;
          cout << "DEVICE: " << pPowerSwitch->name << ", ON: " << pPowerSwitch->isOn() << ", TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
          cout << "SENSORS: [" << soc << ", " << chargersInputPower << ", " << chargersOutputPower << ", " 
               << requiredPowerTotal << ", " << batteryBankVoltage << "]" << endl;
          cout << strLogBuffer;
          //pPowerSwitch->printVerbose(); cout << endl;
        }
      }
      currentDevice = nullptr;

      unsigned long nowMs = automation::millisecs();
      unsigned long elapsedSyncDurationMs = nowMs - syncTimeMs;
      if ( elapsedSyncDurationMs > 30 * MINUTES ) {
        cout << ">>>>>>>>>>>>>>>>>>>>> Synchronizing current state (sending to IFTTTT) <<<<<<<<<<<<<<<<<<<<<<" << endl;
        automation::bSynchronizing = true;
        for (automation::PowerSwitch *pPowerSwitch : devices) {
          automation::clearLogBuffer();
          bool bOn = pPowerSwitch->isOn();
          cout << "DEVICE '" << pPowerSwitch->name << "' = " << (bOn? "ON" : "OFF") << endl;
          pPowerSwitch->setOn(bOn);
          if ( pPowerSwitch->bError ) {
            iDeviceErrorCnt++;
          }
          string strLogBuffer;
          automation::logBufferToString(strLogBuffer);
          cout << strLogBuffer;
        }
        automation::bSynchronizing = false;
        syncTimeMs = nowMs;
      } else if ( bFirstTime ) {
        bFirstTime = false;
      }

      // wait 60 seconds if any request fails (occasional DNS failure or network connectivity)
      automation::sleep( iDeviceErrorCnt ? 60*1000 : 1000 ); 
    };

    cout << "====================================================" << endl;
    cout << "Turning off all switches (application is exiting)..." << endl;
    cout << "====================================================" << endl;
    for (automation::PowerSwitch *pPowerSwitch : devices) {
          automation::clearLogBuffer();
          bool bOn = pPowerSwitch->isOn();
          cout << "DEVICE '" << pPowerSwitch->name << "' = " << (bOn? "ON" : "OFF") << endl;
          pPowerSwitch->setOn(false);
          string strLogBuffer;
          automation::logBufferToString(strLogBuffer);
          cout << strLogBuffer;
    }
    cout << "Exiting " << args[0] << endl;
    return 0;
  }
};

POCO_APP_MAIN(IftttApp);
