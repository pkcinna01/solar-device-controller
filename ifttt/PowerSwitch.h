#ifndef SOLAR_IFTTT_POWERSWITCH_H
#define SOLAR_IFTTT_POWERSWITCH_H

#include "../automation/device/PowerSwitch.h"
#include "ifttt.h"
#include "WebHookSession.h"
#include "WebHookEvent.h"

namespace ifttt {

  class PowerSwitch : public automation::PowerSwitch {
  public:

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

    virtual bool isOn() {
      //TODO - Find way to query samsung smart switch from IFTTT... may need to go directly to Samsung web service
      return bLastValueSent;
    }

    virtual void setOn(bool bOn) {
      WebHookSession session(ifttt::KEY);
      string eventLabel = bOn ? strOnEventLabel : strOffEventLabel;
      WebHookEvent evt(eventLabel);
      if (session.sendEvent(evt)) {
        bLastValueSent = bOn;
      }
    }
  };
}
#endif //SOLAR_IFTTT_TOGGLE_H
