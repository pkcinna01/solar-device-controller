#ifndef AUTOMATION_CAPABILITY_H
#define AUTOMATION_CAPABILITY_H

#include "Poco/Dynamic/Var.h"

#include <string>
#include <functional>

using namespace std;

namespace automation {

  class Capability {

  public:

    const string name;

    std::function<Poco::Dynamic::Var()> readHandler;
    std::function<void(Poco::Dynamic::Var)> writeHandler;

    Capability(const string &name) : name(name) {
    };

    virtual const Poco::Dynamic::Var getValue() const {
      return readHandler();
    };

    virtual void setValue(const Poco::Dynamic::Var &value) {
      writeHandler(value);
    };
  };

}

#endif //AUTOMATION_CAPABILITY_H
