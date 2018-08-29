//
// All non-arduino specific implementations for automation namespace here for now...
//

#include <chrono>
#include <Poco/Thread.h>
#include "Sensor.h"

namespace automation {
  unsigned long millisecs() {
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  void sleep(unsigned long intervalMs) {
    Poco::Thread::sleep(intervalMs);
    //delay(intervalMs);
  }

  void Sensor::print(int depth) {
    cout << *this << endl;
  }

}

