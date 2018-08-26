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
    //std::shared_ptr<Constraint> pConstraint;
    Constraint *pConstraint = NULL;
    Constraint *pPrerequisiteConstraint = NULL;
    vector<Capability *> capabilities;
    bool bConstraintsPassed = false;

    Device(const string &id) :
        id(id) {
    }

    virtual void applyConstraints() {
      if (!pPrerequisiteConstraint || pPrerequisiteConstraint->test() ) {
        if (pConstraint) {
          bool bTest = pConstraint->test();
          if (bTest != bConstraintsPassed) {
            bConstraintsPassed = bTest;
            constraintsResultChanged(bTest);
          }
        }
      }
    }

    virtual void constraintsResultChanged(bool bConstraintResult) = 0;
  };

}


#endif //AUTOMATION_DEVICE_H
