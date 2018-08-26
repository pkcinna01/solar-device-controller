#ifndef AUTOMATION_CONSTRAINT_H
#define AUTOMATION_CONSTRAINT_H

#include <string>
#include <iostream>
#include <chrono>

using namespace std;

namespace automation {

  class Constraint {

    public:

    static unsigned long millisecs() {
      auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
      return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    virtual ~Constraint(){
      cout << getTitle() << " deleted" << endl;
    }
    bool bNegate = false;

    unsigned long passDelayMs = 0, failDelayMs = 0;
    
    virtual float getValue() = 0;
    virtual bool checkValue(float value) = 0;
    virtual string getTitle() { return "Constraint"; }
    
    virtual bool test()
    {
      float value = getValue();
      bool bCheckPassed = checkValue(value);
      if ( bNegate ) {
        bCheckPassed = !bCheckPassed;
      }

      if ( !deferredTimeMs ) {
        // first test so ignore delays on changing state
        deferredTimeMs = millisecs();
        if ( deferredTimeMs == 0 )
          deferredTimeMs++;

        setTestResult(bCheckPassed);
        
      } else if ( bTestResult != bCheckPassed ) {

        if ( deferredResultCnt == 0 ) {
              deferredTimeMs = millisecs();
        }
        if ( bCheckPassed ) {
          if ( deferredDuration() >= passDelayMs ) {
            setTestResult(true);
          } else {
            if ( !deferredResultCnt ) {
              cout << "CONSTRAINT " << getTitle() << ": accept pending..." << endl;
            }
            deferredResultCnt++;
          }
        } else {
          if ( deferredDuration() >= failDelayMs ) {
            setTestResult(false);
          } else {
            if ( !deferredResultCnt ) {
              cout << "CONSTRAINT " << getTitle() << ": reject pending..." << endl;
            }
            deferredResultCnt++;
          }
        }
      } else { // test result has not changed
        if ( deferredResultCnt > 0 ) {
          // a delayed result change was in progress but the original test 
          // result occured so reset change time and deferred cnt
          deferredTimeMs = millisecs();
          setTestResult(bCheckPassed);
        }
      } 

      return bTestResult;
    }

    virtual Constraint& negate() {
      bNegate = true;
      return *this;
    }
    
    virtual Constraint& setPassDelayMs(unsigned long delayMs) {
      passDelayMs = delayMs;
      return *this;
    }
    
    virtual Constraint& setFailDelayMs(unsigned long delayMs) {
      failDelayMs = delayMs;
      return *this;
    }

    virtual Constraint& setPassMargin(float passMargin) {
      this->passMargin = passMargin;
      return *this;
    }

    virtual Constraint& setFailMargin(float margin) {
      this->failMargin = margin;
      return *this;
    }

    protected:

    bool bTestResult = false;
    unsigned long deferredTimeMs = 0;
    unsigned int deferredResultCnt = 0;
    float passMargin = 0;
    float failMargin = 0;

    virtual void setTestResult(bool bTestResult) {

      //unsigned long now = millisecs();
      cout << "CONSTRAINT " << getTitle() << " set to " << bTestResult; // << " after " << (now-deferredTimeMs) << " millisecs";
      if ( bTestResult != this->bTestResult || deferredResultCnt ) {
        if ( bTestResult != this->bTestResult ) {
          cout << " (changed)";
        } else {
          cout << " (reverting to old state after pending change)";
        }
        deferredResultCnt = 0;
        //deferredTimeMs = now;
        this->bTestResult = bTestResult;
      } else {
        cout << " (no change)";
      }
      cout << "." << endl;
    }

    unsigned long deferredDuration() {
        unsigned long nowMs = millisecs();
        //cout << "deferredDuration() now: " << nowMs << ", deferredTimeMs: " << deferredTimeMs << ", duration:" << (nowMs - deferredTimeMs) << endl;
        return (nowMs - deferredTimeMs);
    }

  };
}
#endif