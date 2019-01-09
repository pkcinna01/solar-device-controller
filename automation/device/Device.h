#ifndef AUTOMATION_DEVICE_H
#define AUTOMATION_DEVICE_H

#include <vector>
#include <string>
#include <memory>
#include "../constraint/Constraint.h"
#include "../constraint/BooleanConstraint.h"

using namespace std;

namespace automation {

  class Capability;

  class Device {
  public:

    string name;
    Constraint *pConstraint = nullptr;
    vector<Capability *> capabilities;
    bool bError;

    enum class Mode { OFF, ON=0x1, AUTO=0x2 };
    Mode mode = Mode::AUTO;
    
    static Mode parseMode(const char* pszMode)  {
      if (!strcasecmp("OFF", pszMode))
          return Mode::OFF;
      if (!strcasecmp("ON", pszMode))
        return Mode::ON;
      if (!strcasecmp("AUTO", pszMode))
        return Mode::AUTO;
    }

    static string modeToString(Mode mode) {
      switch(mode) {
        case Mode::OFF: return "OFF";
        case Mode::ON: return "ON";
        default: return "AUTO";
      }
    }

    Device(const string &name) :
        name(name), pConstraint(nullptr), bError(false) {
    }

    virtual void applyConstraint(bool bIgnoreSameState = true, Constraint *pConstraint = nullptr) {
      if ( !pConstraint ) {
        pConstraint = this->pConstraint;
      }
      if (pConstraint) {
        if ( automation::bSynchronizing && !pConstraint->isSynchronizable() ) {
          return;
        }
        if ( mode == Mode::AUTO ) {
          bool bTestResult = pConstraint->test();
          if ( pPrerequisiteConstraint && !pPrerequisiteConstraint->test() ) {
            if ( bConstraintPassed ) {
              bConstraintPassed = false;
              constraintResultChanged(false);
            }
          } else if (!bIgnoreSameState || bTestResult != bConstraintPassed) {
            bConstraintPassed = bTestResult;
            constraintResultChanged(bTestResult);
          }
        }
        else {
          pConstraint->overrideTestResult(mode == Mode::ON);
        }
      }
    }

    virtual void constraintResultChanged(bool bConstraintResult) = 0;

    virtual void print(int depth = 0);
    virtual void printVerbose(int depth = 0 ) { print(depth); }

    virtual Constraint* getConstraint() {
      return pConstraint;
    }

    virtual void setConstraint(Constraint* pConstraint) {
      this->pConstraint = pConstraint;
    }
    
    virtual void setup() = 0;
    
    friend std::ostream &operator<<(std::ostream &os, const Device &d) {
      os << "\"Device\": { \"name\": \"" << d.name << "\" }";
      return os;
    }

  protected:
    Constraint *pPrerequisiteConstraint = nullptr;
    bool bInitialized = false;
    bool bConstraintPassed = false;
  };


  class Devices : public AutomationVector<Device*> {
  public:
    Devices(){}
    Devices( vector<Device*>& devices ) : AutomationVector<Device*>(devices) {}
    Devices( vector<Device*> devices ) : AutomationVector<Device*>(devices) {}
  };


}


#endif //AUTOMATION_DEVICE_H
