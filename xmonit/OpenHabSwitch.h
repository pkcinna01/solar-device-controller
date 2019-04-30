#ifndef XMONIT_OPENHABPOWERSWITCH_H
#define XMONIT_OPENHABPOWERSWITCH_H

#include "../automation/device/PowerSwitch.h"
#include "xmonit.h"

#include <Poco/DynamicStruct.h>
#include <Poco/JSON/Object.h>
#include "Poco/JSON/Parser.h"
#include "Poco/JSON/ParseHandler.h"
#include "Poco/JSON/Stringifier.h"
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>

using namespace Poco::Net;
using namespace Poco::Util;
using namespace Poco;

#include <sstream>

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>

// simple Raspberry PI pin on/off implementation 
// For external relays or powerstrips that just need a power signal
namespace xmonit {


  class OpenHabSwitch : public automation::PowerSwitch {
  public:
    RTTI_GET_TYPE_IMPL(xmonit,OpenHabPowerSwitch);

    string itemName;
    mutable  HTTPClientSession clientSession;
    string openHabItemUrl;

    OpenHabSwitch(const string &title, const string &itemName, float requiredWatts = 0) : 
      automation::PowerSwitch(title,requiredWatts), 
      itemName(itemName),
      clientSession("openhab",8080),
      bLastIsOnCheckResult(false),
      lastIsOnCachedResultTimeMs(0)
     {
      openHabItemUrl = "/rest/items/";
      openHabItemUrl += itemName;
     }

    void setup() override {
    }


    bool isOn() const override {
      ulong nowMs = automation::millisecs();
      if ( nowMs - lastIsOnCachedResultTimeMs > 30000 ) {
        lastIsOnCachedResultTimeMs = nowMs;
        Poco::JSON::Object::Ptr pJsonResp = processRequest(HTTPRequest::HTTP_GET,"");
        Poco::Dynamic::Var itemState = pJsonResp->get("state");
        if ( itemState.isEmpty() ) {
          bError = true;
          automation::logBuffer << __PRETTY_FUNCTION__ << " ERROR" << endl;
        } else {
          bLastIsOnCheckResult = (itemState == "ON");
          lastIsOnCachedResultTimeMs = 0; //automation::millisecs();
          //automation::logBuffer << __PRETTY_FUNCTION__ << " result:" << bLastIsOnCheckResult << endl;
        } 
      }
      return bLastIsOnCheckResult;
    }

    void setOn(bool bOn) override {
      Poco::JSON::Object::Ptr pJsonResp = processRequest(HTTPRequest::HTTP_POST,bOn?"ON":"OFF","text/plain");
      Poco::Dynamic::Var statusVar = pJsonResp->get("status");
      if ( statusVar.isEmpty() || statusVar.convert<int>() != HTTPResponse::HTTP_OK ) {
        bError = true;
        Poco::Dynamic::Var reasonVar = pJsonResp->get("reason");
        automation::logBuffer << __PRETTY_FUNCTION__ << " ERROR: ";
        if ( !reasonVar.isEmpty() ) {
          automation::logBuffer << reasonVar.toString();
        } 
        if ( !statusVar.isEmpty() ) {
          automation::logBuffer << " (" << statusVar.toString() << ")";
        }
        automation::logBuffer << endl;
      } else {
        automation::logBuffer << __PRETTY_FUNCTION__ << " bOn=" << bOn << endl;
        bLastIsOnCheckResult = bOn;
      }
    }

    protected:
    mutable bool bLastIsOnCheckResult;
    mutable ulong lastIsOnCachedResultTimeMs;
    Poco::JSON::Object::Ptr processRequest(const string& httpMethod, const string& httpBody, const string contentType = "application/json") const {
      HTTPRequest req(httpMethod, openHabItemUrl);
      req.setContentType(contentType);
      req.setContentLength(httpBody.length());

      //automation::logBuffer << ">>>>> BEGIN OpenHab Request @ " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << " <<<<<" << endl;
      stringstream ss;
      req.write(ss);
      string requestLine1;
      getline(ss,requestLine1);
      requestLine1.erase(remove(requestLine1.begin(), requestLine1.end(), '\r'), requestLine1.end());      
      automation::logBuffer << requestLine1 << endl;

      ostream &os = clientSession.sendRequest(req);
      os << httpBody;

      HTTPResponse resp;
      Poco::JSON::Object::Ptr resultPtr; 
      istream &is = clientSession.receiveResponse(resp);
      if ( contentType == "application/json" ) {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(is);
        resultPtr = result.extract<Poco::JSON::Object::Ptr>();
      } else {
        Poco::JSON::Object::Ptr pJsonStatus( new Poco::JSON::Object);
        Poco::Dynamic::Var status = (int) resp.getStatus();
        pJsonStatus->set("status", status);
        pJsonStatus->set("reason", resp.getReason());
        resultPtr = pJsonStatus;
      }
      //resultPtr->stringify(automation::logBuffer);
      //automation::logBuffer << endl;
      //automation::logBuffer << ">>>>> END OpenHab Request<<<<<" << endl;

      return resultPtr;
    }

  };
}
#endif //SOLAR_IFTTT_TOGGLE_H
