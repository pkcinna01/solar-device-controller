#ifndef AUTOMATION_H
#define AUTOMATION_H

#include <string>
#include <sstream>

using namespace std;

namespace automation {

  static stringstream logBuffer;

  template<typename T>
  string asString(const T &t) {
    ostringstream os;
    os << t;
    return os.str();
  }

  unsigned long millisecs();

  void sleep(unsigned long intervalMs);
}
#endif