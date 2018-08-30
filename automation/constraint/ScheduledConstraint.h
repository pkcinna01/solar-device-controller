#ifndef AUTOMATION_TIME_CONSTRAINT_H
#define AUTOMATION_TIME_CONSTRAINT_H

#include "Constraint.h"
#include "BooleanConstraint.h"
#include "NestedConstraint.h"

namespace automation {

  class ScheduledConstraint : public NestedConstraint {
  public:

    explicit ScheduledConstraint(Constraint *pConstraint = &automation::PASS_CONSTRAINT) :
        NestedConstraint(pConstraint, "SCHEDULED") {
    }

    bool outerCheckValue(bool bInnerCheckResult) override {
      // TODO - must use arduino compatible classes and know if real time was set exeternally
      return bInnerCheckResult;
    }
  };

}
#endif
