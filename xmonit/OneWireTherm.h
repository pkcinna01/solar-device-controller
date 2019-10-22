#ifndef XMONIT_ONEWIRETHERM_H
#define XMONIT_ONEWIRETHERM_H

// sensor backed by a "one-wire" temp sensor (DS18B20) 

#include "xmonit.h"
#include "../automation/sensor/Sensor.h"

#include "Poco/Util/LayeredConfiguration.h"

#include <memory>


namespace xmonit {

  class OneWireThermSensor : public automation::Sensor {
  public:
    RTTI_GET_TYPE_IMPL(xmonit,OneWireThermSensor);

    std::unique_ptr<automation::SensorListener> pListener;
    string title, filePath;

    OneWireThermSensor(const string &name, const string& filePath, const string &title ) : 
      automation::Sensor(name,1),
      title(title),
      filePath(filePath) {
    }
    
    virtual float getValueImpl() const override;
    
    virtual string getTitle() const override {
      return title;
    }

    static void createSensors(Poco::Util::LayeredConfiguration& conf, vector<unique_ptr<OneWireThermSensor>>& sensors);
     
    private:
    mutable float fLastResult{0};
 };
}
#endif 
