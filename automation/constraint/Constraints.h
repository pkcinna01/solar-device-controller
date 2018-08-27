#ifndef AUTOMATION_CONSTRAINTS_H
#define AUTOMATION_CONSTRAINTS_H

#include "Constraint.h"
#include "../Automation.h"
#include "../Sensor.h"
#include "../capability/Toggle.h"
#include "../device/Device.h"

namespace automation {

  class BooleanConstraint : public Constraint {

    const bool bResult;

  public:
    BooleanConstraint(bool bResult) : bResult(bResult){
    }

    bool checkValue() override {
      return bResult;
    }

    string getTitle() override {
      return bResult?"PASS":"FAIL";
    }
  };

  BooleanConstraint FAIL_CONSTRAINT(false);
  BooleanConstraint PASS_CONSTRAINT(true);

  class CompositeConstraint : public Constraint {
  public:
    vector<Constraint *> constraints;
    string strJoinName;

    CompositeConstraint(const string &strJoinName, const vector<Constraint *> &constraints) :
        strJoinName(strJoinName),
        constraints(constraints) {
    }

    string getTitle() override {
      string title = "(";
      for (int i = 0; i < constraints.size(); i++) {
        title += constraints[i]->getTitle();
        if (i + 1 < constraints.size()) {
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
    AndConstraint(const vector<Constraint *> &constraints) :
        CompositeConstraint("AND", constraints) {
    }

    bool checkValue() override {
      for (Constraint *pConstraint : constraints) {
        if (!pConstraint->test())
          return false;
      }
      return true;
    }
  };

  class OrConstraint : public CompositeConstraint {
  public:
    OrConstraint(const vector<Constraint *> &constraints) :
        CompositeConstraint("OR", constraints) {
    }

    bool checkValue() override {
      for (Constraint *pConstraint : constraints) {
        if (pConstraint->test())
          return true;
      }
      return false;
    }
  };

  class ToggleStateConstraint : public Constraint {
  public:
    Toggle *pToggle;
    bool bAcceptState;

    explicit ToggleStateConstraint(Toggle *pToggle, bool bAcceptState = true) : pToggle(pToggle),
                                                                                bAcceptState(bAcceptState) {
      deferredTimeMs = automation::millisecs(); // make sure first run will apply passDelayMs (give time to prometheus to get readings)
    }

    bool checkValue() override {
      return pToggle->getValue() == bAcceptState;
    }

    string getTitle() override {
      string title = "Toggle must be ";
      title += (bAcceptState ? "ON" : "OFF");
      return title;
    }
  };


  template <typename T>
  class ValueConstraint : public Constraint {

    bool checkValue() override {
      const T &value = getValue();
      bool bCheckPassed = checkValue(value);
      return bCheckPassed;
    }

    virtual T getValue() = 0;

    virtual bool checkValue(const T &val) = 0;
  };

  class SensorConstraint : public ValueConstraint<float> {
  public:
    Sensor &sensor;

    explicit SensorConstraint(Sensor &sensor)
        : sensor(sensor) {
    }

    float getValue() override {
      return sensor.getValue();
    }

  };

  class RangeConstraint : public SensorConstraint {
  public:

    float minVal, maxVal;

    RangeConstraint(float minVal, float maxVal, Sensor &sensor)
        : SensorConstraint(sensor), minVal(minVal), maxVal(maxVal) {
    }

    bool checkValue(const float &value) override {
      float minVal = this->minVal;
      float maxVal = this->maxVal;

      if (deferredTimeMs) {
        if (bTestResult) {
          minVal -= failMargin;
          maxVal += failMargin;
        } else {
          minVal += passMargin;
          maxVal -= passMargin;
        }
      }

      return value >= minVal && value <= maxVal;
    }

    string getTitle() override {
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

    MaxConstraint(float maxVal, Sensor &sensor)
        : SensorConstraint(sensor), maxVal(maxVal) {
    }

    bool checkValue(const float &value) override {
      float maxVal = this->maxVal;

      if (deferredTimeMs) {
        if (bTestResult) {
          maxVal += failMargin;
        } else {
          maxVal -= passMargin;
        }
      }

      return value <= maxVal;
    }

    string getTitle() override {
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

    MinConstraint(float minVal, Sensor &sensor)
        : SensorConstraint(sensor), minVal(minVal) {
    }

    bool checkValue(const float &value) override {
      float minVal = this->minVal;

      if (deferredTimeMs) {
        if (bTestResult) {
          minVal -= failMargin;
        } else {
          minVal += passMargin;
        }
      }
      return value >= minVal;
    }

    string getTitle() override {
      string rtn(sensor.name);
      rtn += " MIN(";
      rtn += asString(minVal);
      rtn += ")";
      return rtn;
    }

  };

}

#endif
