#ifndef IFTTT_WEBHOOKSESSION_H
#define IFTTT_WEBHOOKSESSION_H

#include "WebHookEvent.h"
#include "../automation/Automation.h"
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <string>
#include <iostream>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>

using namespace Poco;
using namespace Poco::Net;
using namespace std;

namespace ifttt {

  class WebHookSession : public HTTPSClientSession {

  public:
    string strKey;

    WebHookSession(const string &iftttKey) :
        HTTPSClientSession("maker.ifttt.com"),
        strKey(iftttKey) {
      setKeepAlive(true);
    }

    bool sendEvent(const WebHookEvent &evt) {
      std::string strPath("/trigger/");
      strPath += evt.strLabel + "/with/key/" + strKey;
      HTTPRequest req(HTTPRequest::HTTP_POST, strPath);
      req.setContentType("application/json");
      //req.setKeepAlive(true);
      string reqBody = "{\"value1\":\"";
      reqBody += evt.strValue1 + "\",\"value2\":\"" + evt.strValue2 + "\",\"value3\":\"" + evt.strValue3 + "\"}";
      req.setContentLength(reqBody.length());

      automation::logBuffer << ">>>>> BEGIN IFTTT WebHook Request @ " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << " <<<<<" << endl;
      stringstream ss;
      req.write(ss);
      string requestLine1;
      getline(ss,requestLine1);
      requestLine1.erase(remove(requestLine1.begin(), requestLine1.end(), '\r'), requestLine1.end());      
      automation::logBuffer << requestLine1 << endl;
      //cout << reqBody << endl;

      ostream &os = sendRequest(req);
      os << reqBody;

      HTTPResponse resp;
      istream &is = receiveResponse(resp);
      string strResp;
      Poco::StreamCopier::copyToString(is, strResp);
      automation::logBuffer << "response: " << strResp << endl;
      automation::logBuffer << ">>>>> END IFTTT WebHook Request" << "<<<<<" << endl;
      return resp.getStatus() == HTTPResponse::HTTPStatus::HTTP_OK;
    }
  };
};
#endif
