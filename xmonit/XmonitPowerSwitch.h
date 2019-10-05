#ifndef XMONIT_POWERSWITCH_H
#define XMONIT_POWERSWITCH_H

#include "../automation/device/PowerSwitch.h"
#include "xmonit.h"
#include "XmonitSession.h"


namespace xmonit {

  class XmonitPowerSwitch : public automation::PowerSwitch {
  public:
    RTTI_GET_TYPE_IMPL(xmonit,PowerSwitch);

    const int MAX_RETRY_CNT = 2;

    bool bLastValueSent;

    string strOnEventLabel, strOffEventLabel;

    XmonitPowerSwitch(const string &id, float requiredWatts = 0) : automation::PowerSwitch(id,requiredWatts), bLastValueSent(false) {}

    virtual PowerSwitch& setOnEventLabel(const string& onLabel) {
      strOnEventLabel = onLabel;
      return *this;
    }

    virtual PowerSwitch& setOffEventLabel(const string& offLabel) {
      strOffEventLabel = offLabel;
      return *this;
    }

    void setup() override {

    }

    bool isOn() const override {
      //TODO - get latest value via USB and have cache rules
      return bLastValueSent;
    }

    void setOn(bool bOn) override {
      bError = false;
      string eventLabel = bOn ? strOnEventLabel : strOffEventLabel;
      auto pSession = std::make_unique<XmonitSession>();

      for( int i = 0; i < MAX_RETRY_CNT; i++) {
        try {
          pSession->sendToggleEvent(name,bOn);
          return;
        } catch (Poco::Exception &ex)  {
          automation::logBuffer << "FAILED turning " << ( bOn ? "ON" : "OFF" ) << " switch '" << name << "' (XMONIT host: " << pSession->getHost() << ")." << endl;
          automation::logBuffer << ex.displayText() << endl;
        }
      }
      bError = true;
    }
  };
}
#endif //SOLAR_IFTTT_TOGGLE_H
