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

    //TODO - remove POCO Var for Arduino code compatibility
    std::function<Poco::Dynamic::Var()> readHandler;
    std::function<void(Poco::Dynamic::Var)> writeHandler;

    Capability(const string &name) : name(name) {
    };

    virtual const Poco::Dynamic::Var getValue() const {
      return readHandler();
    };

    virtual void setValue(const Poco::Dynamic::Var &value) {
      writeHandler(value);
      for( CapabilityListener* pListener : listeners) {
        pListener->valueSet(this,value);
      }
    };

    struct CapabilityListener {
      virtual void valueSet(const Capability* pCapability, double numericValue) = 0;
    };

    vector<CapabilityListener*> listeners;

    virtual void addListener(CapabilityListener* pListener){
      listeners.push_back(pListener);
    }

    virtual void removeListener(CapabilityListener* pListener){
      listeners.erase(std::remove(listeners.begin(), listeners.end(), pListener), listeners.end());
    }
  };

}

#endif //AUTOMATION_CAPABILITY_H
