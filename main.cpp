
#include "ifttt/WebHookSession.h"
#include "ifttt/WebHookEvent.h"
#include "ifttt/PowerSwitch.h"
#include "automation/Automation.h"
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
#include "automation/device/MutualExclusionDevice.h"

#include "Prometheus.h"

#include "Poco/Util/Application.h"

#include <iostream>
#include <numeric>

using namespace std;
using namespace automation;
using namespace ifttt;

// Summer window AC units use 525 (AC + Fan)
// Winter heaters use 575 watts
#define DEFAULT_APPLIANCE_WATTS 575

// Summer window AC units have expensive startup so accept a lower min voltage (24.00).
// Winter heaters min volts 24.50 so batteries have better chance of recovery at end of the day
#define DEFAULT_MIN_VOLTS 24.50

class IftttApp : public Poco::Util::Application {

public:
  virtual int main(const std::vector<std::string> &args) {

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

    // If 500 watts should be allocated to each switch (air conditioner), this scale function
    // will be equivalent to checking for 1000 watts if 2 are running or 1500 for 3.
    auto scaleByOnCountFn = [](float sourceVal)->float{
      long cnt = count_if(devices.begin(),devices.end(),[](automation::PowerSwitch* s){ return s->isOn(); });
      // if current device is on then scale by actual on count, if it is off scale by what new running count will be (add 1)
      long applicableCnt = currentDevice->isOn() ? cnt : cnt+1;
      return sourceVal/applicableCnt;
    };
    static ScaledSensor scaledGeneratedPower(chargersInputPower,scaleByOnCountFn);

    static struct FamilyRoomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      MinConstraint<float,Sensor&> minSoc {45, soc};
      MinConstraint<float,Sensor&> minGeneratedWatts {DEFAULT_APPLIANCE_WATTS, scaledGeneratedPower};
      AndConstraint enoughPower {{&minSoc, &minGeneratedWatts}};
      MinConstraint<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {10,00,0},{16,30,00} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{4*MINUTES,&toggle,0,1};
      AndConstraint familyRmMasterConstraints {{&timeRange,&notSimultaneousToggleOn,&minVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      FamilyRoomMasterSwitch() :
          ifttt::PowerSwitch("Family Room Master") {
        setOnEventLabel("family_room_master_switch_on");
        setOffEventLabel("family_room_master_switch_off");
        fullSoc.setPassDelayMs(30*SECONDS).setFailDelayMs(90*SECONDS).setFailMargin(25);
        minSoc.setPassDelayMs(1*MINUTES).setFailDelayMs(120*SECONDS).setFailMargin(20).setPassMargin(5);
        minGeneratedWatts.setPassDelayMs(30*SECONDS).setFailDelayMs(120*SECONDS).setFailMargin(125).setPassMargin(100);
        minVoltage.setFailDelayMs(15*SECONDS).setFailMargin(0.5);
        //familyRmMasterConstraints.setPassDelayMs(5*MINUTES);
        pConstraint = &familyRmMasterConstraints;
      }
    } familyRoomMasterSwitch;

    static struct SunroomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      MinConstraint<float,Sensor&> minSoc {45, soc};
      MinConstraint<float,Sensor&> minGeneratedWatts {DEFAULT_APPLIANCE_WATTS, scaledGeneratedPower};
      AndConstraint enoughPower {{&minSoc, &minGeneratedWatts}};
      MinConstraint<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {8,30,0},{16,30,00} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{4*MINUTES,&toggle,0,1};
      AndConstraint sunroomMasterConstraints {{&timeRange,&notSimultaneousToggleOn,&minVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      SunroomMasterSwitch() :
          ifttt::PowerSwitch("Sunroom Master") {
        setOnEventLabel("sunroom_master_switch_on");
        setOffEventLabel("sunroom_master_switch_off");
        fullSoc.setPassDelayMs(30*SECONDS).setFailDelayMs(120*SECONDS).setFailMargin(25);
        minSoc.setPassDelayMs(2*MINUTES).setFailDelayMs(45*SECONDS).setFailMargin(20).setPassMargin(10);
        minGeneratedWatts.setPassDelayMs(30*SECONDS).setFailDelayMs(45*SECONDS).setFailMargin(125).setPassMargin(100);
        minVoltage.setFailDelayMs(5*SECONDS).setFailMargin(0.5);
        //minVoltage.setFailDelayMs(15*SECONDS);
        pConstraint = &sunroomMasterConstraints;
      }
    } sunroomMasterSwitch;

    static ToggleStateConstraint familyRoomMasterMustBeOn(&familyRoomMasterSwitch.toggle);
    static ToggleStateConstraint sunroomMasterMustBeOn(&sunroomMasterSwitch.toggle);
    static AndConstraint allMastersMustBeOn( { &familyRoomMasterMustBeOn, &sunroomMasterMustBeOn} );
    allMastersMustBeOn.setPassDelayMs(5*MINUTES);

    static struct FamilyRoomAuxSwitch : ifttt::PowerSwitch {

      MinConstraint<float,Sensor&> minSoc {45, soc};
      MinConstraint<float,Sensor&> minVoltage {DEFAULT_MIN_VOLTS, batteryBankVoltage};
      MinConstraint<float,Sensor&> minGeneratedWatts {DEFAULT_APPLIANCE_WATTS, scaledGeneratedPower};
      AndConstraint enoughPower {{&minSoc, &minGeneratedWatts}};
      MinConstraint<float,Sensor&> fullSoc {100, soc};
      OrConstraint fullSocOrEnoughPower {{&fullSoc, &enoughPower}};
      TimeRangeConstraint timeRange { {11,30,0},{15,30,0} };
      SimultaneousConstraint simultaneousToggleOn {2*MINUTES,&toggle};
      NotConstraint notSimultaneousToggleOn {&simultaneousToggleOn};
      TransitionDurationConstraint minOffDuration{20*MINUTES,&toggle,0,1};
      AndConstraint familyRmAuxConstraints {{&timeRange,&allMastersMustBeOn,&notSimultaneousToggleOn,&minVoltage,&fullSocOrEnoughPower,&minOffDuration}};

      FamilyRoomAuxSwitch() :
          ifttt::PowerSwitch("Family Room Auxiliary") {
        setOnEventLabel("family_room_aux_switch_on");
        setOffEventLabel("family_room_aux_switch_off");
        fullSoc.setPassDelayMs(1*MINUTES).setFailDelayMs(30*SECONDS).setFailMargin(15);
        minSoc.setPassDelayMs(3*MINUTES).setFailDelayMs(45*SECONDS).setFailMargin(25);
        minGeneratedWatts.setPassDelayMs(30*SECONDS).setFailDelayMs(45*SECONDS).setFailMargin(75);
        //pPrerequisiteConstraint = &allMastersMustBeOn;
        pConstraint = &familyRmAuxConstraints;
      }
    } familyRoomAuxSwitch;
    
    devices = {&familyRoomMasterSwitch, &sunroomMasterSwitch, &familyRoomAuxSwitch};

    familyRoomMasterSwitch.simultaneousToggleOn.listen(&sunroomMasterSwitch.toggle).listen(&familyRoomAuxSwitch.toggle);
    familyRoomAuxSwitch.simultaneousToggleOn.listen(&familyRoomMasterSwitch.toggle).listen(&sunroomMasterSwitch.toggle);
    sunroomMasterSwitch.simultaneousToggleOn.listen(&familyRoomMasterSwitch.toggle).listen(&familyRoomAuxSwitch.toggle);

    unsigned long nowMs = automation::millisecs();
    unsigned long syncTimeMs = nowMs;

    bool bFirstTime = true;

    TimeRangeConstraint solarTimeRange({0,0,0},{17,30,0}); // app exits at 5:30pm each day

    while ( solarTimeRange.test() ) {

      for (automation::PowerSwitch *pPowerSwitch : devices) {

        prometheusDs.loadMetrics();

        currentDevice = pPowerSwitch;
        automation::clearLogBuffer();
        bool bIgnoreSameState = !bFirstTime;
        pPowerSwitch->applyConstraint(bIgnoreSameState);
        string strLogBuffer;
        automation::logBufferToString(strLogBuffer);

        if ( !strLogBuffer.empty() ) {
          cout << "=================================================================================" << endl;
          cout << "DEVICE " << pPowerSwitch->name << endl;
          cout << strLogBuffer;
          cout << soc << ", " << chargersInputPower << ", " << chargersOutputPower << ", " << scaledGeneratedPower << ", " << batteryBankVoltage << endl;
          cout << "TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
        }
      }

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
          string strLogBuffer;
          automation::logBufferToString(strLogBuffer);
          cout << strLogBuffer;
        }
        automation::bSynchronizing = false;
        syncTimeMs = nowMs;
      } else if ( bFirstTime ) {
        bFirstTime = false;
      }

      automation::sleep(1000);
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
