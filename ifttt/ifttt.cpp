#include <chrono>
#include <Poco/Thread.h>
#include "../automation/Sensor.h"
#include "../automation/capability/Capability.h"
#include "../automation/device/Device.h"

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

  void Sensor::print(int depth) {
    cout << *this << endl;
  }

  void Device::print(int depth) {
    cout << *this << endl;
  }

  void Capability::print(int depth) {
    cout << *this << endl;
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

}