
#include "ifttt/WebHookSession.h"
#include "ifttt/WebHookEvent.h"
#include "ifttt/PowerSwitch.h"
#include "automation/Automation.h"
#include "automation/constraint/ToggleConstraint.h"
#include "automation/constraint/SimultaneousConstraint.h"
#include "automation/constraint/Constraints.h"
#include "automation/device/Device.h"
#include "automation/device/MutualExclusionDevice.h"

#include "Prometheus.h"

#include "Poco/Util/Application.h"

#include <iostream>
#include <numeric>

using namespace std;
using namespace Poco;
using namespace Poco::Dynamic;
using namespace Poco::Net;
using namespace automation;
using namespace ifttt;

class IftttApp : public Poco::Util::Application {

public:
  virtual int main(const std::vector<std::string> &args) {

    auto metricFilter = [](const Prometheus::Metric &metric) { return metric.name.find("solar") == 0; };

    static Prometheus::DataSource prometheusDs(Prometheus::URL, metricFilter);

    static SensorFn soc("State of Charge", []()->float{ return prometheusDs.metrics["solar_charger_batterySOC"].avg(); });
    static SensorFn generatedPower("Generated Power from Chargers",
                                 []()->float{ return prometheusDs.metrics["solar_charger_outputPower"].total(); });
    static SensorFn batteryBankVoltage("Average Battery Terminal Voltage from Chargers",
                                     []()->float{ return prometheusDs.metrics["solar_charger_outputVoltage"].total(); });

    static SimultaneousConstraint noSimultaneousToggleOn(60*1000);
    noSimultaneousToggleOn.bNegate = true;

    static struct FamilyRoomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {20, soc};
      MinConstraint minGeneratedWatts = {500, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      MinConstraint fullSoc = {95, soc};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};
      AndConstraint familyRmMasterConstraints = {{&noSimultaneousToggleOn,&fullSocOrEnoughPower}};

      FamilyRoomMasterSwitch() :
          ifttt::PowerSwitch("Family Room Master Switch (1st Air Conditioner and Fan)") {

        setOnEventLabel("family_room_master_switch_on");
        setOffEventLabel("family_room_master_switch_off");

        fullSoc.setPassDelayMs(10 * 60000).setFailDelayMs(30000).setFailMargin(55);
        minSoc.setPassDelayMs(3 * 60000).setFailDelayMs(45000).setFailMargin(10).setPassMargin(60);
        minGeneratedWatts.setPassDelayMs(3*60000).setFailDelayMs(120000);
        pConstraint = &familyRmMasterConstraints;
      }
    } familyRoomMasterSwitch;

    static ToggleStateConstraint familyRoomMasterMustBeOn(&familyRoomMasterSwitch.toggle);
    familyRoomMasterMustBeOn.setPassDelayMs(3*60000);

    static struct FamilyRoomAuxSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {25, soc};
      MinConstraint minGeneratedWatts = {950, generatedPower};
      MinConstraint fullSoc = {95, soc};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};
      AndConstraint familyRmAuxConstraints = {{&noSimultaneousToggleOn,&fullSocOrEnoughPower}};

      FamilyRoomAuxSwitch() :
          ifttt::PowerSwitch("Family Room Auxiliary Switch (2nd Air Conditioner)") {

        setOnEventLabel("family_room_aux_switch_on");
        setOffEventLabel("family_room_aux_switch_off");

        fullSoc.setPassDelayMs(10 * 60000).setFailDelayMs(20000).setFailMargin(25);
        minSoc.setPassDelayMs(5 * 60000).setFailDelayMs(30000).setFailMargin(10).setPassMargin(65);
        minGeneratedWatts.setFailDelayMs(60000).setPassDelayMs(5*60000).setFailMargin(200);
        pConstraint = &familyRmAuxConstraints;
        pPrerequisiteConstraint = &familyRoomMasterMustBeOn;
      }
    } familyRoomAuxSwitch;


    static struct SunroomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {25, soc};
      MinConstraint minGeneratedWatts = {950, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      MinConstraint fullSoc = {95, soc};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};
      AndConstraint sunroomMasterConstraints = {{&noSimultaneousToggleOn,&fullSocOrEnoughPower}};
      SunroomMasterSwitch() :
          ifttt::PowerSwitch("Sunroom Master Switch (3rd Air Conditioner and Fan)") {

        setOnEventLabel("sunroom_master_switch_on");
        setOffEventLabel("sunroom_master_switch_off");

        fullSoc.setPassDelayMs(10 * 60000).setFailDelayMs(20000).setFailMargin(25);
        minSoc.setPassDelayMs(5 * 60000).setFailDelayMs(30000).setFailMargin(10).setPassMargin(65);
        minGeneratedWatts.setFailDelayMs(60000).setPassDelayMs(5*60000).setFailMargin(200);
        pConstraint = &sunroomMasterConstraints;
      }
    } sunroomMasterSwitch;


    MutualExclusionDevice mutualExclusionDevice("Secondary Air Conditioners Group",{&familyRoomAuxSwitch,&sunroomMasterSwitch});

    vector<automation::Device *> devices = {&familyRoomMasterSwitch, &mutualExclusionDevice};

    // SimultaneousConstraint needs to be a listener of each capability it will watch as a group
    familyRoomMasterSwitch.toggle.addListener(&noSimultaneousToggleOn);
    familyRoomAuxSwitch.toggle.addListener(&noSimultaneousToggleOn);
    sunroomMasterSwitch.toggle.addListener(&noSimultaneousToggleOn);

    prometheusDs.loadMetrics();

    unsigned long syncTimeMs = 0;

    do {
      prometheusDs.loadMetrics();

      unsigned long nowMs = automation::millisecs();
      unsigned long elapsedSyncTimeMs = nowMs - syncTimeMs;
      if ( elapsedSyncTimeMs > 15 * 60000 ) {
        syncTimeMs = nowMs;
      }
      bool bIgnoreSameState = syncTimeMs != nowMs; // don't send results to ifttt unless state change or time to sync

      for (Device *pDevice : devices) {
        automation::logBuffer.str("");
        automation::logBuffer.clear();
        pDevice->applyConstraint(bIgnoreSameState);
        string strLogBuffer = automation::logBuffer.str();

        if ( !strLogBuffer.empty() ) {
          cout << "=====================================================================" << endl;
          cout << "DEVICE: " << pDevice->id << endl;
          cout << strLogBuffer;
          cout << "TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
        }
      }

      automation::sleep(1000);

    } while (true);
    return 0;
  }
};

POCO_APP_MAIN(IftttApp);
