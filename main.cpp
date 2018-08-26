
#include "ifttt/WebHookSession.h"
#include "ifttt/WebHookEvent.h"
#include "ifttt/PowerSwitch.h"
#include "automation/capability/Toggle.h"
#include "automation/constraint/Constraints.h"

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
          ifttt::PowerSwitch("Family Room Master Switch (1st Air Conditioner and Fan") {

        setOnEventLabel("family_room_master_switch_on");
        setOffEventLabel("family_room_master_switch_off");

        minSoc.setPassDelayMs(3 * 60000).setFailDelayMs(45000).setFailMargin(10).setPassMargin(60);
        minGeneratedWatts.setFailDelayMs(120000).setPassDelayMs(3*60000);
        pConstraint = &fullSocOrEnoughPower;
      }
    } familyRoomMasterSwitch;

    static ToggleStateConstraint familyRoomMasterMustBeOn(&familyRoomMasterSwitch.toggle);

    Device* pActiveDevice;
    static MutualExclusionConstraint onlyOne({},[&pActiveDevice](){return pActiveDevice;},30*60000);

    static struct FamilyRoomAuxSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {30, soc};
      MinConstraint minGeneratedWatts = {1400, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};
      AndConstraint allConstraints = {{&onlyOne,&fullSocOrEnoughPower}};

      FamilyRoomAuxSwitch() :
          ifttt::PowerSwitch("Family Room Auxiliary Switch (2nd Air Conditioner)") {

        setOnEventLabel("family_room_aux_switch_on");
        setOffEventLabel("family_room_aux_switch_off");

        minSoc.setPassDelayMs(15 * 60000).setFailDelayMs(5000).setFailMargin(15).setPassMargin(60);
        minGeneratedWatts.setFailDelayMs(30000).setPassDelayMs(5*60000).setFailMargin(200);
        pConstraint = &allConstraints;
        pPrerequisiteConstraint = &familyRoomMasterMustBeOn;
      }
    } familyRoomAuxSwitch;

    static struct SunroomMasterSwitch : ifttt::PowerSwitch {

      MinConstraint minSoc = {25, soc};
      MinConstraint minGeneratedWatts = {1000, generatedPower};
      AndConstraint enoughPower = {{&minSoc, &minGeneratedWatts}};
      OrConstraint fullSocOrEnoughPower = {{&fullSoc, &enoughPower}};
      AndConstraint allConstraints = {{&onlyOne,&fullSocOrEnoughPower}};

      SunroomMasterSwitch() :
          ifttt::PowerSwitch("Sunroom Master Switch (3rd Air Conditioner and Fan") {

        setOnEventLabel("sunroom_master_switch_on");
        setOffEventLabel("sunroom_master_switch_off");
        minSoc.setPassDelayMs(5 * 60000).setFailDelayMs(30000).setFailMargin(10).setPassMargin(65);
        minGeneratedWatts.setFailDelayMs(60000).setPassDelayMs(5*60000);
        pConstraint = &allConstraints;
      }
    } sunroomMasterSwitch;

    onlyOne.devices = {&familyRoomAuxSwitch,&sunroomMasterSwitch};

    vector<automation::Device *> devices = {&familyRoomMasterSwitch, &familyRoomAuxSwitch, &sunroomMasterSwitch};


    prometheusDs.loadMetrics();

    familyRoomMasterSwitch.applyConstraints();

    // give master switch chance to turn on and have prometheus update with new soc, watts, etc...
    Thread::sleep(15000);

    do {
      prometheusDs.loadMetrics();

      for (Device *pDevice : devices) {
        pActiveDevice = pDevice;
        pDevice->applyConstraints();
      }
      Thread::sleep(5000);

    } while (true);
    return 0;
  }
};

POCO_APP_MAIN(IftttApp);
