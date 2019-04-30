#include "DeviceControllerApp.h"

#include "xmonit/OpenHabSwitch.h"
#include "xmonit/GpioPowerSwitch.h"
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

#include "SolarMetrics.h"
#include "HttpServer.h"

#include <signal.h>
#include <iostream>
#include <numeric>

#include <prometheus/exposer.h>
#include <prometheus/registry.h>

DeviceControllerApp* DeviceControllerApp::pInstance(nullptr);

bool DeviceControllerApp::iSignalCaught = 0;

void DeviceControllerApp::signalHandlerFn (int val) { iSignalCaught = val; }

int DeviceControllerApp::main(const std::vector<std::string> &args)
{

  using namespace prometheus;
  if ( args.size() == 1 ) {
    loadConfiguration(args[0]);
  } else {
    loadConfiguration();
  }
  auto &conf = config();
  std::string strIftttKey = conf.getString("ifttt[@key]");

  ConstraintEventHandlerList::instance.push_back(this);
  cout << "START TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
  cout << "ifttt key: '" << strIftttKey << "'" << endl;
  if ( strIftttKey.empty() ) {
    cerr << "Aborting.  Check configuration file for <ifttt key='...'/>" << endl;
    return -1;
  }

  static auto metricFilter = [](const Prometheus::Metric &metric) { return metric.name.find("solar") == 0 || metric.name.find("arduino_solar") == 0; };

  URI url(conf.getString("prometheus[@solarMetricsUrl]", "http://solar:9202/actuator/prometheus"));
  static Prometheus::DataSource prometheusDs(url, metricFilter);

  static SensorFn soc("State of Charge", []() -> float { return prometheusDs.metrics["solar_charger_batterySOC"].avg(); });
  static SensorFn chargersOutputPower("Chargers Output Power",
                                      []() -> float { return prometheusDs.metrics["solar_charger_outputPower"].total(); });
  static SensorFn chargersInputPower("Chargers Input Power",
                                     []() -> float { return prometheusDs.metrics["solar_charger_inputPower"].total(); });
  static SensorFn batteryBankVoltage("Battery Bank Voltage",
                                     []() -> float { return prometheusDs.metrics["solar_charger_outputVoltage"].avg(); });
  static SensorFn batteryBankPower("Battery Bank Power",
                                   []() -> float { return prometheusDs.metrics["arduino_solar_batteryBankPower"].avg(); });

  static SensorFn requiredPowerTotal("Required Power", []() -> float {
    float requiredWatts = 0;
    for (automation::Device *pDevice : pInstance->devices)
    {
      automation::PowerSwitch *pPowerSwitch = dynamic_cast<automation::PowerSwitch*>(pDevice);
      if ( !pPowerSwitch ) {
        continue;
      }
      if (pPowerSwitch->isOn() || pDevice == pInstance->currentDevice)
      {
        requiredWatts += pPowerSwitch->requiredWatts;
      }
    }
    float battBankWatts = batteryBankPower.getValue();
    if (isnan(battBankWatts))
    {
      return requiredWatts;
    }
    else {
      automation::PowerSwitch *pCurrentPowerSwitch = dynamic_cast<automation::PowerSwitch*>(pInstance->currentDevice);
      if (pCurrentPowerSwitch && !pCurrentPowerSwitch->isOn())
      {
        battBankWatts += pCurrentPowerSwitch->requiredWatts;
      }
      return std::max(requiredWatts, battBankWatts);
    }
  });

  static auto prometheusRegistry = std::make_shared<Registry>();

  struct PowerSwitchMetrics : Capability::CapabilityListener
  {
    prometheus::Family<Gauge> *pGauges;
    prometheus::Gauge *pOnOffGauge;
    automation::PowerSwitch *powerSwitch;

    void init(automation::PowerSwitch *powerSwitch)
    {
      pGauges = &(BuildGauge().Name("solar_ifttt_device").Labels({{"name", powerSwitch->name.c_str()}}).Register(*prometheusRegistry));
      pOnOffGauge = &pGauges->Add({{"metric", "on"}});
      powerSwitch->toggle.addListener(this);
    }
    void valueSet(const Capability *pCapability, float newVal, float oldVal) override
    {
      cout << __PRETTY_FUNCTION__ << " updating prometheus guage to " << newVal << endl;
      pOnOffGauge->Set(newVal);
    }
  };

  static struct FamilyRoomMasterSwitch : xmonit::OpenHabSwitch
  {

    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> cutoffVoltage{22, batteryBankVoltage};
    AtLeast<float, Sensor &> minSoc{MIN_SOC_PERCENT, soc};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AndConstraint enoughPower{{&minSoc, &haveRequiredPower}};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &enoughPower}};
    TimeRangeConstraint timeRange{{9, 45, 0}, {17, 00, 00}};
    SimultaneousConstraint simultaneousToggleOn{2 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{4 * MINUTES, &toggle, 0, 1};
    AndConstraint familyRmMasterConstraints{{&timeRange, &notSimultaneousToggleOn, &minVoltage, &cutoffVoltage, &fullSocOrEnoughPower, &minOffDuration}};
    PowerSwitchMetrics metrics;

    FamilyRoomMasterSwitch() : xmonit::OpenHabSwitch("Family Room Master", "FamilyRoomMaster_Switch", DEFAULT_APPLIANCE_WATTS)
    {
      fullSoc.setPassDelayMs(1 * MINUTES).setFailDelayMs(90 * SECONDS).setFailMargin(25);
      minSoc.setPassDelayMs(2 * MINUTES).setFailDelayMs(120 * SECONDS).setFailMargin(20).setPassMargin(5);
      haveRequiredPower.setPassDelayMs(2 * MINUTES).setFailDelayMs(120 * SECONDS).setFailMargin(75).setPassMargin(100);
      minVoltage.setFailDelayMs(30 * SECONDS).setFailMargin(0.5);
      pConstraint = &familyRmMasterConstraints;
      metrics.init(this);
    }

  } familyRoomMasterSwitch;

  static struct SunroomMasterSwitch : xmonit::OpenHabSwitch
  {

    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> minSoc{MIN_SOC_PERCENT, soc};
    AtLeast<float, Sensor &> cutoffVoltage{22.75, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AndConstraint enoughPower{{&minSoc, &haveRequiredPower}};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &enoughPower}};
    TimeRangeConstraint timeRange{{8, 30, 0}, {17, 15, 00}};
    SimultaneousConstraint simultaneousToggleOn{2 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{4 * MINUTES, &toggle, 0, 1};
    AndConstraint sunroomMasterConstraints{{&timeRange, &notSimultaneousToggleOn, &minVoltage, &cutoffVoltage, &fullSocOrEnoughPower, &minOffDuration}};
    PowerSwitchMetrics metrics;

    SunroomMasterSwitch() : xmonit::OpenHabSwitch("Sunroom Master", "SunroomMaster_Switch", DEFAULT_APPLIANCE_WATTS)
    {
      fullSoc.setPassDelayMs(1 * MINUTES).setFailDelayMs(120 * SECONDS).setFailMargin(25);
      minSoc.setPassDelayMs(2.25 * MINUTES).setFailDelayMs(45 * SECONDS).setFailMargin(20).setPassMargin(10);
      haveRequiredPower.setPassDelayMs(2.25 * MINUTES).setFailDelayMs(45 * SECONDS).setFailMargin(75).setPassMargin(100);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5);
      pConstraint = &sunroomMasterConstraints;
      metrics.init(this);
    }
  } sunroomMasterSwitch;

  static struct FamilyRoomAuxSwitch : xmonit::OpenHabSwitch
  {

    AtLeast<float, Sensor &> minSoc{MIN_SOC_PERCENT, soc};
    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AndConstraint enoughPower{{&minSoc, &haveRequiredPower}};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &enoughPower}};
    TimeRangeConstraint timeRange1{{8, 0, 0}, {12, 0, 0}};
    TimeRangeConstraint timeRange2{{12, 0, 0}, {17, 00, 0}};
    OrConstraint validTimes{{&timeRange1, &timeRange2}};
    SimultaneousConstraint simultaneousToggleOn{2 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{4 * MINUTES, &toggle, 0, 1};
    AndConstraint familyRmAuxConstraints{{&validTimes, /*&allMastersMustBeOn,*/
                                          &notSimultaneousToggleOn, &minVoltage, &fullSocOrEnoughPower, &minOffDuration}};
    PowerSwitchMetrics metrics;

    FamilyRoomAuxSwitch() : xmonit::OpenHabSwitch("Family Room Plant Lights", "FamilyRoomPlantLights_Switch", LIGHTS_SET_1_WATTS)
    {
      fullSoc.setPassDelayMs(0.75 * MINUTES).setFailDelayMs(2 * MINUTES).setFailMargin(15);
      minSoc.setPassDelayMs(2 * MINUTES).setFailDelayMs(45 * SECONDS).setFailMargin(25);
      haveRequiredPower.setPassDelayMs(2 * MINUTES).setFailDelayMs(5 * MINUTES).setFailMargin(50).setPassMargin(10);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5);
      pConstraint = &familyRmAuxConstraints;
      metrics.init(this);
    }
  } familyRoomAuxSwitch;

  static struct Outlet1Switch : xmonit::GpioPowerSwitch
  {

    AtLeast<float, Sensor &> minSoc{MIN_SOC_PERCENT, soc};
    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AndConstraint enoughPower{{&minSoc, &haveRequiredPower}};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &enoughPower}};
    TimeRangeConstraint timeRange1{{8, 0, 0}, {12, 0, 0}};
    TimeRangeConstraint timeRange2{{12, 0, 0}, {18, 00, 0}};
    OrConstraint validTimes{{&timeRange1, &timeRange2}};
    SimultaneousConstraint simultaneousToggleOn{2 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{4 * MINUTES, &toggle, 0, 1};
    AndConstraint plantLightsConstraints{{&validTimes, /*&allMastersMustBeOn,*/
                                          &notSimultaneousToggleOn, &minVoltage, &fullSocOrEnoughPower, &minOffDuration}};
    PowerSwitchMetrics metrics;

    Outlet1Switch() : xmonit::GpioPowerSwitch("Plant Lights", 15 /*GPIO PIN*/, LIGHTS_SET_2_WATTS)
    {
      fullSoc.setPassDelayMs(0.5 * MINUTES).setFailDelayMs(2 * MINUTES).setFailMargin(15);
      minSoc.setPassDelayMs(2 * MINUTES).setFailDelayMs(45 * SECONDS).setFailMargin(25);
      haveRequiredPower.setPassDelayMs(2 * MINUTES).setFailDelayMs(5 * MINUTES).setFailMargin(50).setPassMargin(10);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5);
      pConstraint = &plantLightsConstraints;
      metrics.init(this);
    }
  } outlet1Switch;

  devices.push_back( &outlet1Switch );
  devices.push_back( &familyRoomAuxSwitch );
  devices.push_back( &sunroomMasterSwitch );
  devices.push_back( &familyRoomMasterSwitch );

  json::JsonSerialWriter w;
  string strLogBuffer;

  cout << "============== Begin Device(s) Setup ============" << endl;
  for (auto pDevice : devices)
  {
    cout << "DEVICE: " << pDevice->name << endl;
    automation::clearLogBuffer();
    pDevice->setup();
    automation::logBufferToString(strLogBuffer);
    if (!strLogBuffer.empty())
    {
      cout << strLogBuffer << std::flush;
    }
  }
  cout << "============== End Device(s) Setup ============" << endl
       << endl;

  //w.printlnVectorObj("devices",devices,"",true);
  //w.printlnVectorObj("constraints",Constraint::all(),"",true);

  SimultaneousConstraint::connectListeners({&familyRoomMasterSwitch.simultaneousToggleOn, &familyRoomAuxSwitch.simultaneousToggleOn,
                                            &sunroomMasterSwitch.simultaneousToggleOn, &outlet1Switch.simultaneousToggleOn});

  unsigned long nowMs = automation::millisecs();
  unsigned long syncTimeMs = nowMs;

  bool bFirstTime = true;

  TimeRangeConstraint solarTimeRange({0, 0, 0}, {18, 30, 0});

  struct sigaction action;
  action.sa_handler = signalHandlerFn;
  action.sa_flags = 0;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  automation::clearLogBuffer();

  cout << "============= Begin HttpListener Setup =============" << endl;
  xmonit::HttpServer httpServer;
  httpServer.init(conf);
  httpServer.start();
  cout << "============= End HttpListener Setup =============" << endl;

  Exposer exposer{conf.getString("prometheus[@exportBindAddress]", "127.0.0.1:8095")};

  exposer.RegisterCollectable(prometheusRegistry);

  while ( iSignalCaught == 0)
  {
    if ( !solarTimeRange.test() ) {
      automation::sleep(10 * 1000);
      automation::logBufferToString(strLogBuffer);
      if (!strLogBuffer.empty())
      {
        cout << strLogBuffer << std::flush;
      }
      automation::clearLogBuffer();
      continue;
    }
    int iDeviceErrorCnt = 0;

    for (automation::Device *pDevice : devices)
    {

      if (httpServer.mutex.tryLock(10000))
      {
        currentDevice = pDevice;
        prometheusDs.loadMetrics();

        automation::clearLogBuffer();
        bool bIgnoreSameState = !bFirstTime;
        pDevice->applyConstraint(bIgnoreSameState);
        if (pDevice->bError)
        {
          iDeviceErrorCnt++;
        }
        automation::logBufferToString(strLogBuffer);
        if (!strLogBuffer.empty() )
        {
          cout << "=================================================================================" << endl;
          cout << "DEVICE: " << pDevice->name;
          automation::PowerSwitch *pPowerSwitch = dynamic_cast<automation::PowerSwitch*>(pDevice);
          if ( pPowerSwitch ) {
            cout << ", ON: " << pPowerSwitch->isOn();
          } 
          cout << ", TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
          cout << "SENSORS: [";
          for (auto s : {soc, chargersInputPower, chargersOutputPower, requiredPowerTotal, batteryBankVoltage})
          {
            cout << '"' << s.getTitle() << "\"=" << s.getValue() << ",";
          }
          cout << "]" << std::endl
               << strLogBuffer << std::flush;
          //pPowerSwitch->printVerbose();
          //cout << endl;
        }
        httpServer.mutex.unlock();
      }
      else
      {
        cerr << "ERROR: Failed getting mutex from HttpServer.  Skipping processing of " << pDevice->name << endl;
      }
    }
    currentDevice = nullptr;

    unsigned long nowMs = automation::millisecs();
    unsigned long elapsedSyncDurationMs = nowMs - syncTimeMs;
    if (elapsedSyncDurationMs > 30 * MINUTES)
    {
      cout << ">>>>>>>>>>>>>>>>>>>>> Synchronizing current state (sending to IFTTTT) <<<<<<<<<<<<<<<<<<<<<<" << endl;
      automation::bSynchronizing = true;
      for (automation::Device *pDevice : devices)
      {
        automation::PowerSwitch *pPowerSwitch = dynamic_cast<automation::PowerSwitch*>(pDevice);
        if ( !pPowerSwitch ) {
          continue;
        }
        automation::clearLogBuffer();
        bool bOn = pPowerSwitch->isOn();
        cout << "DEVICE '" << pPowerSwitch->name << "' = " << (bOn ? "ON" : "OFF") << endl;
        pPowerSwitch->setOn(bOn);
        if (pPowerSwitch->bError)
        {
          iDeviceErrorCnt++;
        }
        string strLogBuffer;
        automation::logBufferToString(strLogBuffer);
        cout << strLogBuffer;
      }
      automation::bSynchronizing = false;
      syncTimeMs = nowMs;
    }
    else if (bFirstTime)
    {
      bFirstTime = false;
    }

    // wait 60 seconds if any request fails (occasional DNS failure or network connectivity)
    automation::sleep(iDeviceErrorCnt ? 60 * 1000 : 1000);
  };

  cout << "====================================================" << endl;
  cout << "Turning off all switches (application is exiting)..." << endl;
  cout << "====================================================" << endl;
  for (automation::Device *pDevice : devices)
  {
    automation::PowerSwitch *pPowerSwitch = dynamic_cast<automation::PowerSwitch*>(pDevice);
    if ( !pPowerSwitch ) {
      continue;
    }
    automation::clearLogBuffer();
    bool bOn = pPowerSwitch->isOn();
    cout << "DEVICE '" << pPowerSwitch->name << "' = " << (bOn ? "ON" : "OFF") << endl;
    pPowerSwitch->setOn(false);
    string strLogBuffer;
    automation::logBufferToString(strLogBuffer);
    cout << strLogBuffer;
  }
  httpServer.stop();
  cout << "Exiting " << (args.empty() ? "solar_ifttt" : args[0]) << endl;
  return 0;
}
