#ifndef XMONIT_XMONITSESSION_H
#define XMONIT_XMONITSESSION_H

#include "xmonit.h"
#include "XmonitRequest.h"
#include "../automation/Automation.h"
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeFormat.h>


#include <string>
#include <iostream>

using namespace std;
using namespace Poco::JSON;
using namespace Poco;
using namespace Poco::Net;
using namespace std;

namespace xmonit {

  class XmonitSession : public HTTPClientSession {

  public:

    XmonitSession() :
        HTTPClientSession("solar",9202) {
      setKeepAlive(true);
    }

    

    bool sendToggleEvent(string& deviceFilterName, bool bOn) {
      return sendRequest(ToggleRequest(bOn, deviceFilterName));
    };

    bool sendRequest(XmonitRequest&& req) {
      string path = "/arduino/";
      path += req.requestType;
      string httpMethod = req.requestMethod;
      if ( httpMethod == "update" ) {
        httpMethod = "put";
      }
      return sendRequest( path, httpMethod, req );
    }

    bool sendRequest(const std::string& strPath, const std::string& httpMethod, const XmonitRequest& req ) {
      automation::logBuffer << ">>>>> BEGIN XMonit Service Request @ " << DateTimeFormatter::format(LocalDateTime(), DateTimeFormat::SORTABLE_FORMAT) << " <<<<<" << endl;
      automation::logBuffer << req.toString() << endl;
      HTTPRequest httpRequest(httpMethod, strPath);
      httpRequest.setContentType("application/json");
      //req.setKeepAlive(true);
      Poco::JSON::Object::Ptr obj = req.toJson();
      obj->remove("requestType");
      obj->remove("requestMethod");
      string reqBody = XmonitRequest::toString(obj);
      httpRequest.setContentLength(reqBody.length());
      stringstream rawRequestStream;
      httpRequest.write(rawRequestStream);
      string requestLine1;
      getline(rawRequestStream,requestLine1);
      requestLine1.erase(remove(requestLine1.begin(), requestLine1.end(), '\r'), requestLine1.end());
      automation::logBuffer << "HTTP Request (line 1): " << requestLine1 << endl;
      ostream &os = HTTPClientSession::sendRequest(httpRequest);
      os << reqBody;
      HTTPResponse resp;
      istream &is = receiveResponse(resp);
      string strResp;
      Poco::StreamCopier::copyToString(is, strResp);
      automation::logBuffer << "response: " << strResp << endl;
      automation::logBuffer << ">>>>> END XMonit Service Request" << "<<<<<" << endl;
      return resp.getStatus() == HTTPResponse::HTTPStatus::HTTP_OK;
    }
  };
};
#endif