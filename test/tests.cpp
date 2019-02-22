
#include "automation/Automation.h"
#include "constraint-tests.cpp"

#include <Poco/Util/Application.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>
#include <signal.h>
#include <iostream>
#include <numeric>

using namespace std;
using namespace automation;
using namespace Poco;


bool iSignalCaught = 0;
static void signalHandlerFn (int val) { iSignalCaught = val; }

class LogConstraintEventHandler : public ConstraintEventHandler{
  public:
  void resultDeferred(Constraint* pConstraint,bool bNext,unsigned long delayMs) const override {
    logBuffer << "DEFERRED(next=" << bNext << ",delay=" << delayMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };
  void resultChanged(Constraint* pConstraint,bool bNew,unsigned long lastDurationMs) const override {
    logBuffer << "CHANGED(new=" << bNew << ",lastDuration=" << lastDurationMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };
  //void resultSame(Constraint* pConstraint,bool bVal,unsigned long lastDurationMs) const override {
  //  logBuffer << "SAME(value=" << bVal << ",duration=" << lastDurationMs << "ms): " << pConstraint->getTitle() << endl;
  //};
  void deferralCancelled(Constraint* pConstraint,bool bCurrent,unsigned long lastDurationMs) const override {
    logBuffer << "DEFERRAL CANCELED(current=" << bCurrent << ",duration=" << lastDurationMs/1000.0 << "s): " << pConstraint->getTitle() << endl;
  };
} logConstraintEventHandler;
  


class IftttApp : public Poco::Util::Application {

public:
  virtual int main(const std::vector<std::string> &args) {

    ConstraintEventHandlerList::instance.push_back(&logConstraintEventHandler);
    cout << "START TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;

    ConstraintTests::run();

    cout << "END TIME: " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << endl;
    return 0;
  }
};

POCO_APP_MAIN(IftttApp);
