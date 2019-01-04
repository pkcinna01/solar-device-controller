#ifndef SOLAR_IFTTT_POWERSWITCH_H
#define SOLAR_IFTTT_POWERSWITCH_H

#include "../automation/device/PowerSwitch.h"
#include "ifttt.h"
#include "WebHookSession.h"
#include "WebHookEvent.h"

namespace ifttt {

  class PowerSwitch : public automation::PowerSwitch {
  public:

    const int MAX_RETRY_CNT = 2;

    bool bLastValueSent;

    string strOnEventLabel, strOffEventLabel;

    PowerSwitch(const string &id, float requiredWatts) : automation::PowerSwitch(id,requiredWatts), bLastValueSent(false) {}

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
      //TODO - Find way to query samsung smart switch from IFTTT... may need to go directly to Samsung web service
      return bLastValueSent;
    }

    void setOn(bool bOn) override {
      bError = false;
      string eventLabel = bOn ? strOnEventLabel : strOffEventLabel;
      WebHookEvent evt(eventLabel);
      unique_ptr<WebHookSession> pSession(new WebHookSession(ifttt::KEY));
      for( int i = 0; i < MAX_RETRY_CNT; i++) {
        try {
          if (pSession->sendEvent(evt)) {
            bLastValueSent = bOn;
            return;
          }
        } catch (Poco::Exception &ex)  {
          automation::logBuffer << "FAILED turning " << ( bOn ? "ON" : "OFF" ) << " switch '" << name << "' (IFTTT host: " << pSession->getHost() << ")." << endl;
          automation::logBuffer << ex.displayText() << endl;
        }
      }
      bError = true;
    }
  };
}
#endif //SOLAR_IFTTT_TOGGLE_H
