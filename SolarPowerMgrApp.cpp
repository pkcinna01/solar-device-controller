#include "SolarPowerMgrApp.h"

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

SolarPowerMgrApp* SolarPowerMgrApp::pInstance(nullptr);

bool SolarPowerMgrApp::iSignalCaught = 0;

void SolarPowerMgrApp::signalHandlerFn (int val) { iSignalCaught = val; }

int SolarPowerMgrApp::main(const std::vector<std::string> &args)
{

  using namespace prometheus;
  if ( args.size() == 1 ) {
    loadConfiguration(args[0]);
  } else {
    loadConfiguration();
  }
  auto &conf = config();
  //std::string strIftttKey = conf.getString("ifttt[@key]");

  ConstraintEventHandlerList::instance.push_back(this);
  cout << "START TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
  //cout << "ifttt key: '" << strIftttKey << "'" << endl;
  //if ( strIftttKey.empty() ) {
  //  cerr << "Aborting.  Check configuration file for <ifttt key='...'/>" << endl;
  //  return -1;
  //}
  static float maxInputPower = (float) conf.getDouble("maxInputPower",1700); // load is kept below available input power or this configured max
  static float maxOutputPower = (float) conf.getDouble("maxOutputPower",2200); // load is kept below available input power or this configured max
                                                                             // workaround for defective thermal fuse tripping too soon
  cout << "app.xml: maxInputPower=" << maxInputPower << endl;

  static auto metricFilter = [](const Prometheus::Metric &metric) { return metric.name.find("solar") == 0 
                                                                    || metric.name.find("arduino_solar") == 0
                                                                    /*|| metric.name.find("system_temperature_celcius") == 0*/; };

  URI url(conf.getString("prometheus[@solarMetricsUrl]", "http://solar:9202/actuator/prometheus"));
  static Prometheus::DataSource prometheusDs(url, metricFilter);

  static SensorFn soc("State of Charge", []() -> float { return prometheusDs.metrics["solar_charger_batterySOC"].avg(); });
  static SensorFn chargersInputPower("Chargers Input Power",
                                     []() -> float { return std::min(prometheusDs.metrics["solar_charger_inputPower"].total(),maxInputPower); });
  static SensorFn batteryBankVoltage("Battery Bank Voltage",
                                     []() -> float { return prometheusDs.metrics["solar_charger_outputVoltage"].avg(); });
  static SensorFn batteryBankPower("Battery Bank Power",
                                  //TODO - adjust arduino current and voltage sensors for more accurate reading. for now just compensate to reduce it
                                   []() -> float { return prometheusDs.metrics["arduino_solar_batteryBankPower"].avg() * 0.965;  });
  //Control raspberry pi fan... just run the fan all the time for now since this requires extra transistor and no room.                                   
  /*static SensorFn pi1Temp("jaxpi1 Temperature (F)",
                          []() -> float { 
                            float rtnTemp = prometheusDs.metrics["system_temperature_celcius"].avg(); // will only be one
                            if ( !isnan(rtnTemp) ) {
                              rtnTemp = rtnTemp * 1.8 + 32;
                            }
                            return rtnTemp;
                          });
  */                                 
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

  static struct HvacSwitch : xmonit::OpenHabSwitch
  {    
    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> cutoffVoltage{23.75, batteryBankVoltage};
    AtMost<float, Sensor&> cutoffPower{maxOutputPower,batteryBankPower};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &haveRequiredPower}};
    TimeRangeConstraint timeRange;
    SimultaneousConstraint simultaneousToggleOn{4 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{4 * MINUTES, &toggle, 0, 1};
    AndConstraint hvacConstraints{{&timeRange, &notSimultaneousToggleOn, &minVoltage, &cutoffVoltage, &fullSocOrEnoughPower, &minOffDuration, &cutoffPower}};
    PowerSwitchMetrics metrics;

    HvacSwitch(const string& title, const string& openHabItem, const TimeRangeConstraint::Time& start, const TimeRangeConstraint::Time& end) : 
      xmonit::OpenHabSwitch(title, openHabItem, DEFAULT_APPLIANCE_WATTS), 
      timeRange(start,end)
    {
      fullSoc.setPassDelayMs(1 * MINUTES).setFailDelayMs(3 * MINUTES).setFailMargin(25);
      haveRequiredPower.setPassDelayMs(30 * SECONDS).setFailDelayMs(1 * MINUTES).setFailMargin(80).setPassMargin(25);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5);
      pConstraint = &hvacConstraints;
      metrics.init(this);
    }

  } familyRoomHvac1Switch("Family Room HVAC 1", "FamilyRoomHvac1_Switch", {10, 00, 0}, {17, 00, 00}),
    familyRoomHvac2Switch("Family Room HVAC 2", "FamilyRoomHvac2_Switch", {11, 30, 0}, {15, 30, 00}),
    sunroomHvacSwitch("Sunroom HVAC", "SunroomMaster_Switch", {9, 30, 0}, {18, 00, 00});
  
  familyRoomHvac2Switch.minVoltage.setFailDelayMs(45 * SECONDS);
  familyRoomHvac2Switch.cutoffVoltage.setFixedThreshold(23.75);
  familyRoomHvac2Switch.haveRequiredPower.setFailDelayMs(1.5 * MINUTES);
  sunroomHvacSwitch.minVoltage.setFailDelayMs(1.5 * MINUTES);
  sunroomHvacSwitch.cutoffVoltage.setFixedThreshold(23.25);
  sunroomHvacSwitch.cutoffPower.setFailDelayMs(30*SECONDS);
  sunroomHvacSwitch.haveRequiredPower.setFailDelayMs(2.5 * MINUTES);


  static ToggleStateConstraint sunroomHvacOff{&sunroomHvacSwitch.toggle,false}; 
  static ToggleStateConstraint familyRoomHvac1Off{&familyRoomHvac1Switch.toggle,false}; 
  static ToggleStateConstraint familyRoomHvac2Off{&familyRoomHvac2Switch.toggle,false}; 
  static OrConstraint oneOrMoreHvacsOff{{&familyRoomHvac1Off,&familyRoomHvac2Off,&sunroomHvacOff}};

  static struct FamilyRoomAuxSwitch : xmonit::OpenHabSwitch
  {

    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &haveRequiredPower}};
    TimeRangeConstraint timeRange1{{8, 0, 0}, {16, 00, 0}};
    TimeRangeConstraint timeRange2{{18, 00, 0}, {19, 00, 0}};
    OrConstraint validTime{{&timeRange1,&timeRange2}};
    SimultaneousConstraint simultaneousToggleOn{1 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{2 * MINUTES, &toggle, 0, 1};

    AndConstraint familyRmAuxConstraints{{&validTime, &oneOrMoreHvacsOff, &notSimultaneousToggleOn, 
      &minVoltage, &fullSocOrEnoughPower, &minOffDuration}};

    PowerSwitchMetrics metrics;

    FamilyRoomAuxSwitch() : xmonit::OpenHabSwitch("Family Room Plant Lights", "FamilyRoomPlantLights_Switch", LIGHTS_SET_1_WATTS)
    {
      fullSoc.setPassDelayMs(0.75 * MINUTES).setFailDelayMs(2 * MINUTES).setFailMargin(15);
      haveRequiredPower.setPassDelayMs(2 * MINUTES).setFailDelayMs(5 * MINUTES).setFailMargin(50).setPassMargin(10);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5);
      pConstraint = &familyRmAuxConstraints;
      metrics.init(this);
    }
  } familyRoomAuxSwitch;

  static struct Outlet1Switch : xmonit::GpioPowerSwitch
  {

    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS+0.2, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &haveRequiredPower}};
    TimeRangeConstraint timeRange1{{8, 0, 0}, {16, 00, 0}};
    TimeRangeConstraint timeRange2{{18, 00, 0}, {19, 00, 0}};
    OrConstraint validTime{{&timeRange1,&timeRange2}};
    SimultaneousConstraint simultaneousToggleOn{1 * MINUTES, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{2 * MINUTES, &toggle, 0, 1};
    AndConstraint plantLightsConstraints{{&validTime, &notSimultaneousToggleOn, &minVoltage, 
                                          &fullSocOrEnoughPower, &minOffDuration, &oneOrMoreHvacsOff}};
    PowerSwitchMetrics metrics;

    Outlet1Switch() : xmonit::GpioPowerSwitch("Plant Lights", 15 /*GPIO PIN*/, LIGHTS_SET_2_WATTS)
    {
      fullSoc.setPassDelayMs(0.5 * MINUTES).setFailDelayMs(2 * MINUTES).setFailMargin(15);
      haveRequiredPower.setPassDelayMs(2 * MINUTES).setFailDelayMs(5 * MINUTES).setFailMargin(50).setPassMargin(10);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5);
      pConstraint = &plantLightsConstraints;
      metrics.init(this);
    }
  } outlet1Switch;

  /*static struct RaspberryPi1Fan : xmonit::GpioPowerSwitch
  {

    AtLeast<float, Sensor &> fanOnThreashold{144, pi1Temp};
    PowerSwitchMetrics metrics;

    RaspberryPi1Fan() : xmonit::GpioPowerSwitch("jaxpi1 Fan", 14)
    {
      fanOnThreashold.setPassDelayMs( 30*SECONDS ).setFailDelayMs( 2*MINUTES ).setFailMargin(4);
      pConstraint = &fanOnThreashold;
      metrics.init(this);
    }
  } pi1Fan;*/

  devices.push_back( &outlet1Switch );
  devices.push_back( &familyRoomAuxSwitch );
  devices.push_back( &sunroomHvacSwitch );
  devices.push_back( &familyRoomHvac1Switch );
  devices.push_back( &familyRoomHvac2Switch );
  //devices.push_back( &pi1Fan );

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

  w.printlnVectorObj("devices",devices,"",true);
  //w.printlnVectorObj("constraints",Constraint::all(),"",true);


  SimultaneousConstraint::connectListeners({&familyRoomHvac1Switch.simultaneousToggleOn, &familyRoomHvac2Switch.simultaneousToggleOn,
    &familyRoomAuxSwitch.simultaneousToggleOn, &sunroomHvacSwitch.simultaneousToggleOn, &outlet1Switch.simultaneousToggleOn});

  unsigned long nowMs = automation::millisecs();
  //unsigned long syncTimeMs = nowMs;

  bool bFirstTime = true;

  TimeRangeConstraint solarTimeRange({0, 0, 0}, {19, 45, 0});

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
    if ( !solarTimeRange.test() || !bEnabled ) {
      automation::sleep(5 * 1000);
      automation::logBufferToString(strLogBuffer);
      if (!strLogBuffer.empty())
      {
        cout << strLogBuffer << std::flush;
      }
      automation::clearLogBuffer();
      continue;
    }
    int iDeviceErrorCnt = 0;
    
    vector<Device*> turnedOffSwitches;

    prometheusDs.loadMetrics();

    for (automation::Device *pDevice : devices)
    {

      if (httpServer.mutex.tryLock(10000))
      {
        currentDevice = pDevice;
        //prometheusDs.loadMetrics();

        automation::clearLogBuffer();
        bool bIgnoreSameState = !bFirstTime;
        bool bIsOn = pDevice->isPassed();
        pDevice->applyConstraint(bIgnoreSameState);
        if ( bIsOn && !pDevice->isPassed() ) {
          turnedOffSwitches.push_back(pDevice);
        }
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
          for (auto s : {soc, chargersInputPower, requiredPowerTotal, batteryBankVoltage})
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

    for (automation::Device *pDevice : turnedOffSwitches) {
      // put at end of list so other devices get higher priority (rotates air conditioners better)
      auto it = std::find(devices.begin(),devices.end(),pDevice);
      std::rotate(it, it + 1, devices.end());
    }


    unsigned long nowMs = automation::millisecs();
    /*unsigned long elapsedSyncDurationMs = nowMs - syncTimeMs;
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
    else */
    if (bFirstTime)
    {
      bFirstTime = false;
    }

    // wait 60 seconds if any request fails (occasional DNS failure or network connectivity)
    automation::sleep(iDeviceErrorCnt ? 60 * 1000 : 1500);
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
