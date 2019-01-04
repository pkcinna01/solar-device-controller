#ifndef ARDUINO_SOLAR_SKETCH_COOLINGFAN_H
#define ARDUINO_SOLAR_SKETCH_COOLINGFAN_H

#include "../Sensor.h"
#include "PowerSwitch.h"
#include "../constraint/ValueConstraint.h"

using namespace std;

namespace automation {

  class CoolingFan: public PowerSwitch {

  public:

    Sensor& tempSensor;
    MinConstraint<float,Sensor&> minTemp; // any temp greater than this min will PASS (turn fan on)

    CoolingFan(const string &name, Sensor& tempSensor, float onTemp, float offTemp, unsigned int minDurationMs=0) :
        PowerSwitch(name),
        tempSensor(tempSensor),
        minTemp(onTemp,tempSensor) {
      minTemp.setFailMargin(onTemp-offTemp).setFailDelayMs(minDurationMs); // temp below min is considered a fail which turns off fan
      pConstraint = &minTemp;
    }

  };

}
#endif //ARDUINO_SOLAR_SKETCH_COOLINGFAN_H