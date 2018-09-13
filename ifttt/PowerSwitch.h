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


    PowerSwitch(const string &id) : automation::PowerSwitch(id), bLastValueSent(false) {}

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
      WebHookSession session(ifttt::KEY);
      string eventLabel = bOn ? strOnEventLabel : strOffEventLabel;
      WebHookEvent evt(eventLabel);

      for( int i = 0; i < MAX_RETRY_CNT; i++) {
        if (session.sendEvent(evt)) {
          bLastValueSent = bOn;
          break;
        }
      }
    }
  };
}
#endif //SOLAR_IFTTT_TOGGLE_H
