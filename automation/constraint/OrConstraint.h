#ifndef AUTOMATION_ORCONSTRAINT_H
#define AUTOMATION_ORCONSTRAINT_H

#include "CompositeConstraint.h"

namespace automation {

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

}

#endif
