#ifndef AUTOMATION_CAPABILITY_H
#define AUTOMATION_CAPABILITY_H

#include <string>
#include <iostream>
#include <sstream>
#include <functional>

using namespace std;

namespace automation {

  // Try to handle any value type but lean on double for now
  //
  class Capability {

  public:

    const string name;

    double value = 0;

    Capability(const string &name) : name(name) {
    };

    virtual double getValueImpl() = 0;
    virtual void setValueImpl(double dVal) = 0;

    virtual double getValue() {
      return getValueImpl();
    }

    virtual void setValue(double dVal) {
      setValueImpl(dVal);
      notifyListeners();
    };

    virtual bool asBoolean() {
      return getValue() != 0;
    }

    virtual const string asString() {
      ostringstream ss;
      ss << getValue() << endl;
      return ss.str();
    }

    virtual void setValue(bool bVal) {
      setValue( (double) (bVal ? 1.0 : 0) );
    };

    virtual void setValue(const string& strVal) {
      std::istringstream ss(strVal);
      double d;
      if (ss >> d ) {
        setValue(d);
      } else {
        std::cerr << "WARNING " << __PRETTY_FUNCTION__ <<  " failed parsing " + strVal + " to double." << endl;
      }
    }

    virtual void notifyListeners() {
      for( CapabilityListener* pListener : listeners) {
        pListener->valueSet(this,getValue());
      }
    };

    struct CapabilityListener {
      virtual void valueSet(const Capability* pCapability, double value) = 0;
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
