#ifndef AUTOMATION_SIMULTANEOUS_CONSTRAINT_H
#define AUTOMATION_SIMULTANEOUS_CONSTRAINT_H

#include "Constraint.h"
#include "../capability/Capability.h"

namespace automation {

class SimultaneousConstraint : public Constraint, public Capability::CapabilityListener {
  public:
    unsigned int maxIntervalMs = 1000;
    unsigned int lastPassTimeMs = 0;

    explicit SimultaneousConstraint(unsigned int maxIntervalMs) :
      maxIntervalMs(maxIntervalMs) {
    }

    // considered simultaneous if last non-zero capability result happened within maxIntervalMs millisecs
    bool checkValue() override {
      unsigned long now = millisecs();
      bool bLastPassRecent = now - lastPassTimeMs <= maxIntervalMs;
      if ( bLastPassRecent ) {
        cout << __PRETTY_FUNCTION__ << " last PASS was recent (considered simultaneous)" << endl;
      } else {
        //cout << __PRETTY_FUNCTION__ << " last PASS was NOT recent (considered NOT simultaneous)" << endl;
      }
      return bLastPassRecent;
    }

    string getTitle() override {
      string title = "Simultaneous";
      return title;
    }

    // add instance of this class as listener to a capability if it is in group of things to be throttled
    // Example:
    // Toggle t1, t2, t3; // make sure these
    // SimultaneousConstraint simultaneousConstraint(60*1000);
    // t1.addListener(simultaneousConstraint);
    // t2.addListener(simultaneousConstraint);
    // t3.addListener(simultaneousConstraint);
    //
    void valueSet(const Capability* pCapability, double numericValue) override {
      if ( numericValue != 0 ) {
        cout << __PRETTY_FUNCTION__ << " " << pCapability->name << " lastPassTimeMs was: " << lastPassTimeMs;
        lastPassTimeMs = millisecs();
        cout << ", now: " << lastPassTimeMs << endl;
      }
    }

  };

}
#endif
