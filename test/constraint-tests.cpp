
#include "automation/Automation.h"
#include "automation/json/JsonStreamWriter.h"
#include "automation/sensor/Sensor.h"
#include "automation/device/Device.h"
#include "automation/constraint/ConstraintEventHandler.h"
#include "automation/constraint/NotConstraint.h"
#include "automation/constraint/AndConstraint.h"
#include "automation/constraint/OrConstraint.h"
#include "automation/constraint/BooleanConstraint.h"
#include "automation/constraint/ValueConstraint.h"
#include "automation/constraint/ToggleConstraint.h"
#include "automation/constraint/SimultaneousConstraint.h"
#include "automation/constraint/TimeRangeConstraint.h"
#include "automation/constraint/TransitionDurationConstraint.h"

#include <signal.h>
#include <iostream>
#include <numeric>

using namespace std;
using namespace automation;




struct ConstraintTests {

    struct TestToggle : public automation::Toggle {
      double v {0};
      public:
      TestToggle() : automation::Toggle(nullptr) {}
      virtual double getValueImpl() const { return v; }
      virtual bool setValueImpl(double dVal) { v = dVal; }
    };

public:


  static void run() {

    TestToggle t1, t2, t3, t4;
    SimultaneousConstraint c1(15*SECONDS,&t1);
    SimultaneousConstraint c2(15*SECONDS,&t2);
    SimultaneousConstraint c3(15*SECONDS,&t3);
    SimultaneousConstraint c4(15*SECONDS,&t4);
    vector<SimultaneousConstraint*> constraints{ &c1, &c2, &c3, &c4 };

    SimultaneousConstraint::connectListeners({&c1, &c2, &c3, &c4});    


    for( int i = 0; i < 10; i++ ) {

      int j = 0;
      for( SimultaneousConstraint* pConstraint : constraints ) {
        j++;
        cout << "====== START " << j << "======" << endl;
        automation::clearLogBuffer();
        string strLogBuffer;
        if ( !pConstraint->pCapability->getValue() && !pConstraint->test() ) {
          cout << "Setting constraint " << j << endl;
          pConstraint->pCapability->setValue(true);
        }
        automation::logBufferToString(strLogBuffer);
        if ( !strLogBuffer.empty() ) {
          cout << strLogBuffer;
        }
        //pConstraint->printVerbose();
        cout << "ON: " << pConstraint->pCapability->getValue() << ", simultaineous: " << pConstraint->test() << endl;
       cout << "====== END " << j << "======" << endl;
      }  
      automation::sleep( 5000 ); 
    };
  }
};

