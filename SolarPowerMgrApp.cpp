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
#include "automation/Cacheable.h"
#include "xmonit/OneWireTherm.h"

#include "SolarMetrics.h"
#include "HttpServer.h"

#include <signal.h>
#include <iostream>
#include <numeric>
#include <memory>

#include <prometheus/gauge.h>
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
  
  ConstraintEventHandlerList::instance.push_back(this);
  cout << "START TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
  
  static float maxInputPower = (float) conf.getDouble("maxInputPower",1700); // load is kept below available input power or this configured max
  static float maxOutputPower = (float) conf.getDouble("maxOutputPower",2200); // load is kept below available input power or this configured max
                                                                               // workaround for defective thermal fuse tripping too soon
  ulong maxSensorCacheAgeMs = conf.getDouble("maxSensorCacheAgeMs",15000); // min period before reloading prometheus metrics from solar-web-service
  ulong errorPauseMs = conf.getDouble("errorPauseMs",60000);  
  ulong idlePauseMs = conf.getDouble("idlePauseMs",5000);

  cout << "app.xml: maxInputPower=" << maxInputPower << endl;

  static auto metricFilter = [](const Prometheus::Metric &metric) { return metric.name.find("solar") == 0 
                                                                    || metric.name.find("arduino_solar") == 0; };

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
  
  sensors.push_back(&soc);
  sensors.push_back(&chargersInputPower);
  sensors.push_back(&batteryBankVoltage);
  sensors.push_back(&batteryBankPower);

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

  // add some sensors directly to prometheus export so we can track misc temps in grafana
  vector<std::unique_ptr<xmonit::OneWireThermSensor>> oneWireThermSensors;
  xmonit::OneWireThermSensor::createSensors(conf,oneWireThermSensors);

  struct SensorMetric : automation::SensorListener {
    prometheus::Gauge *pGauge;
    SensorMetric(prometheus::Family<Gauge> *pSensorGauges, map<string,string>& labels){
      pGauge = &pSensorGauges->Add(labels);
    }
    void changed(const Sensor* pSensor, float newVal, float oldVal) const override {
      pGauge->Set(newVal);
    }   
  };

  prometheus::Family<Gauge> *pSensorGauges = &(BuildGauge().Name("solar_power_mgr_sensor").Labels({{"type", "statistic"}}).Register(*prometheusRegistry));
  for( auto& s : sensors) {
    map<string,string> labels = {{"name",s->name}};
    s->pListener = unique_ptr<SensorMetric>(new SensorMetric(pSensorGauges,labels));
  }
  
  pSensorGauges = &(BuildGauge().Name("solar_power_mgr_sensor").Labels({{"type", "OneWireTherm"}}).Register(*prometheusRegistry));
  for( auto& s : oneWireThermSensors) {
    sensors.push_back(s.get());
    map<string,string> labels = {{"metric", "celciusTemp"},{"name",s->name}, {"title",s->getTitle()}};
    s->pListener = unique_ptr<SensorMetric>(new SensorMetric(pSensorGauges,labels));
  }

  struct PowerSwitchMetrics : Capability::CapabilityListener
  {
    prometheus::Family<Gauge> *pGauges;
    prometheus::Gauge *pOnOffGauge;
    automation::PowerSwitch *powerSwitch;

    void init(automation::PowerSwitch *powerSwitch)
    {
      pGauges = &(BuildGauge().Name("solar_power_mgr_device").Labels({{"name", powerSwitch->name.c_str()}}).Register(*prometheusRegistry));
      pOnOffGauge = &pGauges->Add({{"metric", "on"}});
      powerSwitch->toggle.addListener(this);
    }
    void valueSet(const Capability *pCapability, float newVal, float oldVal) override
    {
      automation::logBuffer << __PRETTY_FUNCTION__ << " updating prometheus guage to " << newVal << endl;
      pOnOffGauge->Set(newVal);
    }
  };

  static struct HvacSwitch : xmonit::OpenHabSwitch
  {    
    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> cutoffVoltage{23.60, batteryBankVoltage};
    AtMost<float, Sensor&> cutoffPower{maxOutputPower,batteryBankPower};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &haveRequiredPower}};
    TimeRangeConstraint timeRange;
    SimultaneousConstraint simultaneousToggleOn{2 * MINUTES, &toggle}; // should be larger than fullSoc fail delay
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{4 * MINUTES, &toggle, 0, 1};
    AndConstraint hvacConstraints{{&timeRange, &notSimultaneousToggleOn, &minVoltage, &cutoffVoltage, &fullSocOrEnoughPower, &minOffDuration, &cutoffPower}};
    PowerSwitchMetrics metrics;

    HvacSwitch(const string& title, const string& openHabItem, const TimeRangeConstraint::Time& start, const TimeRangeConstraint::Time& end) : 
      xmonit::OpenHabSwitch(title, openHabItem, DEFAULT_APPLIANCE_WATTS), 
      timeRange(start,end)
    {
      fullSoc.setPassDelayMs(1 * MINUTES).setFailDelayMs(1.75 * MINUTES); // large fail delay so haveRequiredPower has time to decide. real cutoff is voltage
      haveRequiredPower.setPassDelayMs(30 * SECONDS).setFailDelayMs(1.5 * MINUTES).setFailMargin(90).setPassMargin(30);
      minVoltage.setFailDelayMs(60 * SECONDS).setFailMargin(0.5).setPassMargin(1.25);
      cutoffVoltage.setPassMargin(3.0).setPassDelayMs(4*MINUTES);
      setConstraint(&hvacConstraints);
      metrics.init(this);
    }

  } familyRoomHvac2Switch("Family Room HVAC 1", "FamilyRoomHvac1_Switch", {11, 45, 0}, {14, 45, 00}),
    familyRoomHvac1Switch("Family Room HVAC 2", "FamilyRoomHvac2_Switch", {10, 00, 0}, {17, 00, 00}),
    sunroomHvacSwitch("Sunroom HVAC", "SunroomMaster_Switch", {9, 30, 0}, {18, 00, 00});
  
  familyRoomHvac2Switch.minVoltage.setFailDelayMs(45 * SECONDS);
  familyRoomHvac2Switch.cutoffVoltage.setFixedThreshold(23.70);
  familyRoomHvac2Switch.haveRequiredPower.setPassDelayMs(1 * MINUTES).setFailDelayMs(1 * MINUTES);
  sunroomHvacSwitch.minVoltage.setFailDelayMs(1.0 * MINUTES);
  sunroomHvacSwitch.cutoffVoltage.setFixedThreshold(23.50);
  sunroomHvacSwitch.cutoffPower.setFailDelayMs(30*SECONDS);
  sunroomHvacSwitch.haveRequiredPower.setPassDelayMs(30 * SECONDS).setFailDelayMs(2.5 * MINUTES);


  static ToggleStateConstraint sunroomHvacOff{&sunroomHvacSwitch.toggle,false}; 
  static ToggleStateConstraint familyRoomHvac1Off{&familyRoomHvac1Switch.toggle,false}; 
  static ToggleStateConstraint familyRoomHvac2Off{&familyRoomHvac2Switch.toggle,false}; 
  static OrConstraint oneOrMoreHvacsOff{{&familyRoomHvac1Off,&familyRoomHvac2Off,&sunroomHvacOff}};

  struct OpenHabLightSwitch : xmonit::OpenHabSwitch
  {

    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &haveRequiredPower}};
    TimeRangeConstraint timeRange1{{8, 0, 0}, {16, 00, 0}};
    TimeRangeConstraint timeRange2{{16, 00, 0}, {18, 00, 0}};
    OrConstraint validTime{{&timeRange1,&timeRange2}};
    SimultaneousConstraint simultaneousToggleOn{30 * SECONDS, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{2 * MINUTES, &toggle, 0, 1};

    AndConstraint familyRmAuxConstraints{{&validTime, &oneOrMoreHvacsOff, &notSimultaneousToggleOn, 
      &minVoltage, &fullSocOrEnoughPower, &minOffDuration}};

    PowerSwitchMetrics metrics;

    OpenHabLightSwitch(const string& name, const string& strOpenHabId,float requiredWatts) : xmonit::OpenHabSwitch(name,strOpenHabId,requiredWatts)
    {
      fullSoc.setPassDelayMs(0.75 * MINUTES).setFailDelayMs(1.5 * MINUTES).setFailMargin(15);
      haveRequiredPower.setPassDelayMs(1 * MINUTES).setFailDelayMs(2.5 * MINUTES).setFailMargin(25).setPassMargin(10);
      minVoltage.setFailDelayMs(45 * SECONDS).setFailMargin(0.5);
      setConstraint(&familyRmAuxConstraints);
      metrics.init(this);
    }
  };
  
  OpenHabLightSwitch familyRoomAuxSwitch("Family Room Plant Lights", "FamilyRoomPlantLights_Switch", LIGHTS_SET_1_WATTS);
  OpenHabLightSwitch diningRoomAuxSwitch("Dining Room Plant Lights", "Sonoff_Switch_2", LIGHTS_SET_1_WATTS);
  
  static struct PlantLightsSwitch : xmonit::GpioPowerSwitch
  {

    AtLeast<float, Sensor &> minVoltage{DEFAULT_MIN_VOLTS+0.2, batteryBankVoltage};
    AtLeast<float, Sensor &> haveRequiredPower{requiredPowerTotal, chargersInputPower};
    AtLeast<float, Sensor &> fullSoc{FULL_SOC_PERCENT, soc};
    OrConstraint fullSocOrEnoughPower{{&fullSoc, &haveRequiredPower}};
    TimeRangeConstraint timeRange1{{8, 0, 0}, {16, 00, 0}};
    TimeRangeConstraint timeRange2{{16, 00, 0}, {18, 00, 0}};
    OrConstraint validTime{{&timeRange1,&timeRange2}};
    SimultaneousConstraint simultaneousToggleOn{30 * SECONDS, &toggle};
    NotConstraint notSimultaneousToggleOn{&simultaneousToggleOn};
    TransitionDurationConstraint minOffDuration{2 * MINUTES, &toggle, 0, 1};
    AndConstraint plantLightsConstraints{{&validTime, &notSimultaneousToggleOn, &minVoltage, 
                                          &fullSocOrEnoughPower, &minOffDuration, &oneOrMoreHvacsOff}};
    PowerSwitchMetrics metrics;

    PlantLightsSwitch() : xmonit::GpioPowerSwitch("Plant Lights", 15 /*GPIO PIN*/, LIGHTS_SET_2_WATTS)
    {
      fullSoc.setPassDelayMs(0.5 * MINUTES).setFailDelayMs(1.5 * MINUTES).setFailMargin(15);
      haveRequiredPower.setPassDelayMs(1 * MINUTES).setFailDelayMs(2.5 * MINUTES).setFailMargin(25).setPassMargin(10);
      minVoltage.setFailDelayMs(45 * SECONDS).setFailMargin(0.5);
      setConstraint(&plantLightsConstraints);
      metrics.init(this);
    }
  } gpioPlantLightsSwitch;

  devices.push_back( &gpioPlantLightsSwitch );
  devices.push_back( &familyRoomAuxSwitch );
  devices.push_back( &diningRoomAuxSwitch );
  devices.push_back( &sunroomHvacSwitch );
  devices.push_back( &familyRoomHvac1Switch );
  devices.push_back( &familyRoomHvac2Switch );
  
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


  SimultaneousConstraint::connectListeners({&familyRoomHvac1Switch.simultaneousToggleOn, &familyRoomHvac2Switch.simultaneousToggleOn,
    &familyRoomAuxSwitch.simultaneousToggleOn, &sunroomHvacSwitch.simultaneousToggleOn, &gpioPlantLightsSwitch.simultaneousToggleOn,
    &diningRoomAuxSwitch.simultaneousToggleOn});

  unsigned long nowMs = automation::millisecs();

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

    static ulong lastResultTimeMs = 0;
    
    ulong nowMs = automation::millisecs();

    {
      Poco::Mutex::ScopedLock lock(httpServer.mutex);
      
      if ( nowMs - lastResultTimeMs > maxSensorCacheAgeMs ) {
        prometheusDs.loadMetrics(); 
        for ( auto& s : sensors ) {
          if( !dynamic_cast<automation::Cacheable<float>*>(s) ) {
            s->reset().getValue(); // arduino compatible sensors cache value by default so call reset to clear cached value
          }
        }
        for ( auto& d : devices ) {
          automation::PowerSwitch *pPowerSwitch = dynamic_cast<automation::PowerSwitch*>(d);
          if ( pPowerSwitch ) {
            // calling isOn will force openhab and gpio switches to check if 
            // local cached value needs to be updated with remote value.  
            // Prometheus and Grafana need switch state even if not in solarTimeRange
            pPowerSwitch->isOn(); 
          }
        }
        lastResultTimeMs = nowMs;
      }

      for ( auto& s : sensors ) {
        automation::Cacheable<float>* pCacheable = dynamic_cast<automation::Cacheable<float>*>(s);
        if ( pCacheable ) {
          pCacheable->getCachedValue(); // each sensor tracks its own last cached time
        }
      }
    }

    bool bProcessDevices = solarTimeRange.test() && bEnabled;

    if ( !bProcessDevices ) {
      automation::sleep(idlePauseMs);
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
    
    for (automation::Device *pDevice : devices)
    {
      Poco::Mutex::ScopedLock lock(httpServer.mutex);

      currentDevice = pDevice;
      automation::PowerSwitch *pPowerSwitch = dynamic_cast<automation::PowerSwitch*>(pDevice);

      automation::clearLogBuffer();
      bool bIgnoreSameState = !bFirstTime;
      pDevice->applyConstraint(bIgnoreSameState);
      bool bIsOn = pPowerSwitch ? pPowerSwitch->isOn() : pDevice->isPassed(); // call isOn() to get remote value (can change from openhab interface)
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
        cout << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << endl;
        cout << "DEVICE: " << pDevice->name;
        if ( pPowerSwitch ) {
          cout << ", ON: " << bIsOn;
        } 
        cout << ", TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
        cout << "SENSORS: [";
        for (auto s : {&soc, &chargersInputPower, &requiredPowerTotal, &batteryBankVoltage})
        {
          cout << '"' << s->getTitle() << "\"=" << s->getValue() << ",";
        }
        cout << "]" << std::endl
              << strLogBuffer << std::flush;
        //pPowerSwitch->printVerbose();
        //cout << endl;
        cout << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << flush;
      }
    }
    currentDevice = nullptr;

    for (automation::Device *pDevice : turnedOffSwitches) {
      // put at end of list so other devices get higher priority (rotates air conditioners better)
      auto it = std::find(devices.begin(),devices.end(),pDevice);
      std::rotate(it, it + 1, devices.end());
    }

    nowMs = automation::millisecs();

    if (bFirstTime)
    {
      bFirstTime = false;
    }

    // wait 60 seconds if any request fails (occasional DNS failure or network connectivity)
    automation::sleep(iDeviceErrorCnt ? errorPauseMs : maxSensorCacheAgeMs);
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

std::ostream& SolarPowerMgrApp::printConstraintTitleAndValue(std::ostream& os, Constraint* pConstraint) const {
  CompositeConstraint* pComposite = dynamic_cast<CompositeConstraint*>(pConstraint);
  if ( pComposite ) {
    os << "(";     
    for (size_t i = 0; i < pComposite->getChildren().size(); i++) {
      printConstraintTitleAndValue(os,pComposite->getChildren()[i]);
      if (i + 1 < pComposite->getChildren().size()) {
        os << " " << pComposite->getJoinName() << " ";
      }
    }
    os << ")" << "=" << pConstraint->isPassed();
  } else {
    os << pConstraint->getTitle() << "=" << pConstraint->isPassed();
  }
  return os;
}

