#include <chrono>
#include <Poco/Thread.h>
#include "../automation/Automation.h"

#include <sstream>

// platform specifics

namespace automation {

  unsigned long millisecs() {
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  void sleep(unsigned long intervalMs) {
    Poco::Thread::sleep(intervalMs);
    //delay(intervalMs);
  }

  bool isTimeValid() {
    return true; // assume time always usable for server applications (not arduino)
  }

  static stringstream logBufferImpl;

  std::ostream& getLogBufferImpl() {
    return logBufferImpl;
  }

  void clearLogBuffer() {
    logBufferImpl.str("");
    logBufferImpl.clear();
  }

  void logBufferToString( string& dest) {
    dest = logBufferImpl.str();
  }

  void threadKeepAliveReset() { 
    // only for Arduino
  }

}