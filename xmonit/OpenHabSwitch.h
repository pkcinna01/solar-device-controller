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
      Constraint* pConstraint = getConstraint();
      if ( pConstraint ) {
        // This should let incoming commands (see HttpServer.cpp) temporarily turn things on/off
        // Things turned on/off in openhab do not use the HttpServer to toggle on/off so this only 
        // matters for clients connecting through HttpServer.cpp (java SolarWebService)
        pConstraint->setRemoteExpiredOp(new Constraint::RemoteExpiredDelayOp(2*MINUTES));
        pConstraint->mode = (automation::Constraint::REMOTE_MODE|automation::Constraint::TEST_MODE);
      }
    }

    SetCode setAttribute(const char* pszKey, const char* pszVal, ostream* pRespStream = nullptr) override {
      SetCode resultCode = automation::PowerSwitch::setAttribute(pszKey, pszVal, pRespStream);
      if ( resultCode == SetCode::OK ) {
        string strKey = pszKey;
        Constraint* pConstraint = getConstraint();
        if ( pConstraint && Poco::toLower(strKey) == "on" ) {
          pConstraint->pRemoteExpiredOp->reset();
        }
      }
      return resultCode;
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
          bError = false;
          bool bOnFromOpenHab = itemState == "ON";
          
          /* open hab or the actual power switch on the device may have changed on/off status so treat that like a remote set command */
          Constraint* pConstraint = getConstraint();
          if ( pConstraint && pConstraint->isPassed() != bOnFromOpenHab  ) {            
            pConstraint->overrideTestResult(bOnFromOpenHab);
            pConstraint->pRemoteExpiredOp->reset();
            automation::logBuffer << __PRETTY_FUNCTION__ << "=" << bOnFromOpenHab << " '" << this->getTitle() << "' change from openhab so updated contraint (openhab state: " 
              << itemState.toString() << ")" << endl;
          }
          
          bLastIsOnCheckResult = bOnFromOpenHab;
          lastIsOnCachedResultTimeMs = automation::millisecs();
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
	      bError = false;
        automation::logBuffer << __PRETTY_FUNCTION__ << " '" << this->getTitle() << "' bOn=" << bOn << endl;
        bLastIsOnCheckResult = bOn;
        lastIsOnCachedResultTimeMs = automation::millisecs();
        Constraint* pConstraint = getConstraint();
        if ( pConstraint ) {
          pConstraint->overrideTestResult(bOn);
        }
      }
    }

    protected:
    mutable bool bLastIsOnCheckResult;
    mutable ulong lastIsOnCachedResultTimeMs;


    Poco::JSON::Object::Ptr processRequest(const string& httpMethod, const string& httpBody, const string contentType = "application/json") const {
      string httpHeaderLine1;
      Poco::JSON::Object::Ptr resultPtr; 
      try{
        HTTPRequest req(httpMethod, openHabItemUrl);
        req.setContentType(contentType);
        req.setContentLength(httpBody.length());

        //automation::logBuffer << ">>>>> BEGIN OpenHab Request @ " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << " <<<<<" << endl;
        stringstream ss;
        req.write(ss);
        getline(ss,httpHeaderLine1);
        httpHeaderLine1 = Poco::replace(httpHeaderLine1,'\r');
        //automation::logBuffer << requestLine1 << endl;

        ostream &os = clientSession.sendRequest(req);
        os << httpBody;

        HTTPResponse resp;
        istream &is = clientSession.receiveResponse(resp);
        if ( contentType == "application/json" ) {
          Poco::JSON::Parser parser;
          Poco::Dynamic::Var result = parser.parse(is);
          resultPtr = result.extract<Poco::JSON::Object::Ptr>();
        } else {
          resultPtr = new Poco::JSON::Object;
          Poco::Dynamic::Var status = (int) resp.getStatus();
          resultPtr->set("status", status);
          resultPtr->set("reason", resp.getReason());
        }
      }
      catch (Poco::Exception &ex)
      {
        cerr << __PRETTY_FUNCTION__ << " ERROR: Failed HTTP request: " <<  httpHeaderLine1 << endl;
        cerr << ex.displayText() << endl;
        resultPtr = new Poco::JSON::Object;
        resultPtr->set("status", 500);
        resultPtr->set("reason", ex.displayText() );
      }
      //resultPtr->stringify(automation::logBuffer);
      //automation::logBuffer << endl;
      //automation::logBuffer << ">>>>> END OpenHab Request<<<<<" << endl;

      return resultPtr;
    }

  };
}
#endif //XMONIT_OPENHABPOWERSWITCH_H
