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

    virtual void applyConstraints(bool bIgnoreSameState = true, Constraint* pConstraint = nullptr) {
      if ( !pConstraint ) {
        pConstraint = this->pConstraint;
      }
      if (!pPrerequisiteConstraint || pPrerequisiteConstraint->test() ) {
        if (pConstraint) {
          bool bTest = pConstraint->test();
          if (!bIgnoreSameState || bTest != bConstraintsPassed) {
            bConstraintsPassed = bTest;
            constraintsResultChanged(bTest);
          }
        }
      }
    }

    virtual void constraintsResultChanged(bool bConstraintResult) = 0;

    virtual bool testConstraint() {
      return pConstraint ? pConstraint->test() : true;
    }

  protected:
    Constraint *pConstraint = nullptr;
    Constraint *pPrerequisiteConstraint = nullptr;
    vector<Capability *> capabilities;
    bool bConstraintsPassed = false;
  };


}


#endif //AUTOMATION_DEVICE_H
