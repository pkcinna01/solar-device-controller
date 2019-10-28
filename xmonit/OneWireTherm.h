#ifndef XMONIT_ONEWIRETHERM_H
#define XMONIT_ONEWIRETHERM_H

// sensor backed by a "one-wire" temp sensor (DS18B20) 

#include "xmonit.h"
#include "../automation/sensor/Sensor.h"
#include "../automation/Cacheable.h"

#include "Poco/Util/LayeredConfiguration.h"

namespace xmonit {

  class OneWireThermSensor : public automation::Sensor, public automation::Cacheable<float> {
  public:
    RTTI_GET_TYPE_IMPL(xmonit,OneWireThermSensor);

    string title, filePath;
    unsigned long maxCacheAgeMs;

    OneWireThermSensor(const string &name, const string& filePath, const string &title, const unsigned long maxCacheAgeMs = 60000 ) : 
      automation::Sensor(name,1),
      title(title),
      filePath(filePath),
      maxCacheAgeMs(maxCacheAgeMs) {
        
    }
    
    virtual float getValueImpl() const override;
    
    virtual string getTitle() const override {
      return title;
    }

    virtual float getValueNow() const override {
      float nowVal = ((Sensor*)this)->reset().getValue();
      //cout << __PRETTY_FUNCTION__ << " nowVal=" << nowVal << endl;
      return nowVal;
    }

    virtual unsigned long getMaxCacheAgeMs() const override {
      return maxCacheAgeMs;
    }

    static void createSensors(Poco::Util::LayeredConfiguration& conf, vector<unique_ptr<OneWireThermSensor>>& sensors);
     
 };
}
#endif 
