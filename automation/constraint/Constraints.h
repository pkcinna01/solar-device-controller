#ifndef AUTOMATION_CONSTRAINTS_H
#define AUTOMATION_CONSTRAINTS_H

#include "Constraint.h"
#include "../Automation.h"
#include "../Sensor.h"
#include "../capability/Toggle.h"
#include "../device/Device.h"

namespace automation {

class CompositeConstraint : public Constraint {
public:
  vector<Constraint*> constraints;
  string strJoinName;

  explicit CompositeConstraint(const string& strJoinName, const vector<Constraint*> constraints) :
    strJoinName(strJoinName),
    constraints(constraints) {
  }

  virtual float getValue() {
    // not used so just return zero
    return 0;
  }

  virtual string getTitle() {
    string title = "(";
    for ( int i = 0; i < constraints.size(); i++ ) {
      title += constraints[i]->getTitle();
      if ( i + 1 < constraints.size() ) {
        title += " ";
        title += strJoinName;
        title += " ";
      }
    }
    title += ")";
    return title;
  }
};

class AndConstraint : public CompositeConstraint {
public:
  AndConstraint(const vector<Constraint*> constraints) :
    CompositeConstraint("AND",constraints){
  }

  virtual bool checkValue(float notUsed) {
    for ( Constraint* pConstraint : constraints ) {
      if ( !pConstraint->test() )
        return false;
    }
    return true;
  }
};

class OrConstraint : public CompositeConstraint {
public:
  OrConstraint(const vector<Constraint*> constraints) :
    CompositeConstraint("OR",constraints){
  }

  virtual bool checkValue(float notUsed) {
    for ( Constraint* pConstraint : constraints ) {
      if ( pConstraint->test() )
        return true;
    }
    return false;
  }
};

class ToggleStateConstraint : public Constraint {
public:
  Toggle* pToggle;
  bool bAcceptState;
  ToggleStateConstraint(Toggle* pToggle, bool bAcceptState = true) : pToggle(pToggle), bAcceptState(bAcceptState) {
  }

  virtual float getValue() {
    return pToggle->getValue();
  }
  virtual bool checkValue(float fToggled) {
    return fToggled != 0;
  }
};


class MutualExclusionConstraint : public Constraint {
public:
  vector<Device*> devices;
  unsigned long maxSelectedDurationMs;
  Device* pSelectedDevice = NULL;
  function<Device*()> getActiveDeviceFn;

  MutualExclusionConstraint(const vector<Device*>& devices, const function<Device*()>& getActiveDeviceFn, unsigned long maxSelectedDurationMs = 0) :
    devices(devices),
    getActiveDeviceFn(getActiveDeviceFn),
    maxSelectedDurationMs(maxSelectedDurationMs){
  }

  virtual float getValue() {
    return 0;
  }

  virtual bool checkValue(float notUsed) {
    Device* pActiveDevice = getActiveDeviceFn();
    if ( !pSelectedDevice ) {
      pSelectedDevice = pActiveDevice;
    }
    cout << "MutualExclusionConstraint::checkValue activeDevice is " << pActiveDevice->id;
    //unsigned long selectedDuration = now - selectedTimeMs;
    if ( pSelectedDevice == pActiveDevice ) {
      // are constraints good and has it been selected for less than maxSelectedDuration
    } else {

    }
    return true;
  }
};

class SensorConstraint : public Constraint {
  public:
  Sensor& sensor;
  
  SensorConstraint(Sensor& sensor) 
  : sensor(sensor)
  {
  }  

  virtual float getValue() {
    return sensor.getValue();
  }
};

class RangeConstraint : public SensorConstraint {
  public:
  
  float minVal, maxVal;
  
  RangeConstraint(float minVal, float maxVal, Sensor& sensor ) 
  : SensorConstraint(sensor), minVal(minVal), maxVal(maxVal) 
  {
  }
  
  virtual bool checkValue(float value) {
    float minVal = this->minVal;
    float maxVal = this->maxVal;

    if ( deferredTimeMs ) {
      if ( bTestResult ) {
        minVal-=failMargin;
        maxVal+=failMargin;
      } else {
        minVal+=passMargin;
        maxVal-=passMargin;
      }
    }

    return value >= minVal && value <= maxVal;
  }

  virtual string getTitle() {
    string rtn(sensor.name);
    rtn += " RANGE(";
    rtn += asString(minVal);
    rtn += ",";
    rtn += asString(maxVal);
    rtn += ")"; 
    return rtn;
  }
};

class MaxConstraint : public SensorConstraint {
  public:
  
  float maxVal;
  
  MaxConstraint(float maxVal, Sensor& sensor) 
  : SensorConstraint(sensor), maxVal(maxVal) 
  {
  }
  
  virtual bool checkValue(float value) {
    float maxVal = this->maxVal;

    if ( deferredTimeMs ) {
      if ( bTestResult ) {
        maxVal+=failMargin;
      } else {
        maxVal-=passMargin;
      }
    }

    return value <= maxVal;
  }

  virtual string getTitle() {
    string rtn(sensor.name);
    rtn += " MAX(";
    rtn += asString(maxVal);
    rtn += ")"; 
    return rtn;
  }

};

class MinConstraint : public SensorConstraint {
  public:
  
  float minVal;
  
  MinConstraint(float minVal, Sensor& sensor) 
  : SensorConstraint(sensor), minVal(minVal) 
  {
  }
  
  virtual bool checkValue(float value) {
    float minVal = this->minVal;
    
    if ( deferredTimeMs ) {
      if ( bTestResult ) {
        minVal-=failMargin;
      } else {
        minVal+=passMargin;
      }
    }
    return value >= minVal;
  }

  virtual string getTitle() {
    string rtn(sensor.name);
    rtn += " MIN(";
    rtn += asString(minVal);
    rtn += ")"; 
    return rtn;
  }

};

}

#endif
