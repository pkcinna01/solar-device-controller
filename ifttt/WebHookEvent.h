#ifndef IFTTT_WEBHOOKEVENT_H
#define IFTTT_WEBHOOKEVENT_H

#include <string>

using namespace std;

namespace ifttt {

  class WebHookEvent {
  public:
    std::string strLabel;
    std::string strValue1, strValue2, strValue3;

    WebHookEvent(const string &strLabel) : strLabel(strLabel) {
    }

    WebHookEvent &setValue1(const string &value) {
      strValue1 = value;
      return *this;
    }

    WebHookEvent &setValue2(const string &value) {
      strValue2 = value;
      return *this;
    }

    WebHookEvent &setValue3(const string &value) {
      strValue3 = value;
      return *this;
    }
  };
};

#endif
