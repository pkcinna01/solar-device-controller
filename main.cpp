
#include "ifttt/WebHookSession.h"
#include "ifttt/WebHookEvent.h"
#include "ifttt/PowerSwitch.h"
#include "automation/Automation.h"
#include "automation/capability/Toggle.h"
#include "automation/constraint/Constraints.h"
#include "automation/device/Device.h"
#include "automation/device/MutualExclusionDevice.h"

#include "Prometheus.h"

#include "Poco/Util/Application.h"
#include <Poco/Thread.h>

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

    Prometheus::DataSource prometheusDs(Prometheus::URL, metricFilter);

    static Sensor soc("State of Charge",
                      [&prometheusDs]() { return prometheusDs.metrics["solar_charger_batterySOC"].avg(); });
    static Sensor generatedPower("Generated Power from Chargers",
                                 [&prometheusDs]() { return prometheusDs.metrics["solar_charger_outputPower"].total(); });
    static Sensor batteryBankVoltage("Average Battery Terminal Voltage from Chargers",
                                     [&prometheusDs]() { return prometheusDs.metrics["solar_charger_outputVoltage"].total(); });
    static MinConstraint fullSoc = {95, soc};
    fullSoc.setPassDelayMs(5 * 60000);

    static struct FamilyRoomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {20, soc};
      MinConstraint minGeneratedWatts = {550, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};

      FamilyRoomMasterSwitch() :
          ifttt::PowerSwitch("Family Room Master Switch (1st Air Conditioner and Fan)") {

        setOnEventLabel("family_room_master_switch_on");
        setOffEventLabel("family_room_master_switch_off");

        minSoc.setPassDelayMs(3 * 60000).setFailDelayMs(45000).setFailMargin(10).setPassMargin(60);
        minGeneratedWatts.setPassDelayMs(3*60000).setFailDelayMs(120000);
        pConstraint = &fullSocOrEnoughPower;
      }
    } familyRoomMasterSwitch;

    static ToggleStateConstraint familyRoomMasterMustBeOn(&familyRoomMasterSwitch.toggle);
    familyRoomMasterMustBeOn.setPassDelayMs(30000);

    static struct FamilyRoomAuxSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {25, soc};
      MinConstraint minGeneratedWatts = {1000, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};

      FamilyRoomAuxSwitch() :
          ifttt::PowerSwitch("Family Room Auxiliary Switch (2nd Air Conditioner)") {

        setOnEventLabel("family_room_aux_switch_on");
        setOffEventLabel("family_room_aux_switch_off");

        minSoc.setPassDelayMs(5 * 60000).setFailDelayMs(30000).setFailMargin(10).setPassMargin(65);
        minGeneratedWatts.setFailDelayMs(60000).setPassDelayMs(5*60000).setFailMargin(200);
        pConstraint = &fullSocOrEnoughPower;
        pPrerequisiteConstraint = &familyRoomMasterMustBeOn;
      }
    } familyRoomAuxSwitch;

    static struct SunroomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {25, soc};
      MinConstraint minGeneratedWatts = {1000, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};

      SunroomMasterSwitch() :
          ifttt::PowerSwitch("Sunroom Master Switch (3rd Air Conditioner and Fan)") {

        setOnEventLabel("sunroom_master_switch_on");
        setOffEventLabel("sunroom_master_switch_off");
        minSoc.setPassDelayMs(5 * 60000).setFailDelayMs(30000).setFailMargin(10).setPassMargin(65);
        minGeneratedWatts.setFailDelayMs(60000).setPassDelayMs(5*60000).setFailMargin(200);
        pConstraint = &fullSocOrEnoughPower;
      }
    } sunroomMasterSwitch;


    MutualExclusionDevice mutualExclusionDevice("Secondary Air Conditioners Group",{&familyRoomAuxSwitch,&sunroomMasterSwitch});

    vector<automation::Device *> devices = {&familyRoomMasterSwitch, &mutualExclusionDevice};

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
        pDevice->applyConstraints(bIgnoreSameState);
        string strLogBuffer = automation::logBuffer.str();

        if ( !strLogBuffer.empty() ) {
          cout << "=====================================================================" << endl;
          cout << "DEVICE: " << pDevice->id << endl;
          cout << "TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
          cout << strLogBuffer;
        }
      }

      Thread::sleep(5000);

    } while (true);
    return 0;
  }
};

POCO_APP_MAIN(IftttApp);
