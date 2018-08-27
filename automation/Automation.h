#ifndef AUTOMATION_H
#define AUTOMATION_H

#include <string>
#include <sstream>
#include <chrono>

using namespace std;

namespace automation {

  template<typename T>
  string asString(const T &t) {
    ostringstream os;
    os << t;
    return os.str();
  }

  static unsigned long millisecs() {
    auto duration = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  static stringstream logBuffer;

}
#endif