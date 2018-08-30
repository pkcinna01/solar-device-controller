//
// Created by pkcinna on 8/21/18.
//

#ifndef AUTOMATION_DEVICE_H
#define AUTOMATION_DEVICE_H

#include <vector>
#include <string>
#include <memory>
#include "../constraint/Constraint.h"
#include "../capability/Capability.h"

using namespace std;

namespace automation {

  class Device {
  public:

    string id;

    Device(const string &id) :
        id(id) {
    }

    virtual void applyConstraint(bool bIgnoreSameState = true, Constraint *pConstraint = nullptr) {
      if ( !pConstraint ) {
        pConstraint = this->pConstraint;
      }
      if (pConstraint) {
        bool bTestResult = pConstraint->test();
        bool bPrereqOk = !pPrerequisiteConstraint || pPrerequisiteConstraint->test();
        if ( bPrereqOk || !bTestResult ) { // allow setting to false/fail even if prerequisite not met
          if (!bIgnoreSameState || bTestResult != bConstraintPassed) {
            bConstraintPassed = bTestResult;
            constraintResultChanged(bTestResult);
          }
        }
      }
    }

    virtual void constraintResultChanged(bool bConstraintResult) = 0;

    virtual bool testConstraint() {
      return pConstraint ? pConstraint->test() : true;
    }

  protected:
    Constraint *pConstraint = nullptr;
    Constraint *pPrerequisiteConstraint = nullptr;
    vector<Capability *> capabilities;
    bool bConstraintPassed = false;
  };


}


#endif //AUTOMATION_DEVICE_H
