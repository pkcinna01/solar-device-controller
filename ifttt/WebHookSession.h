#ifndef IFTTT_WEBHOOKSESSION_H
#define IFTTT_WEBHOOKSESSION_H

#include "ifttt.h"
#include "WebHookEvent.h"
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <string>
#include <iostream>

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

            cout << endl << ">>>>> BEGIN IFTTT WebHook Request" << endl;
            req.write(std::cout);
            //cout << reqBody << endl;

            ostream &os = sendRequest(req);
            os << reqBody;

            HTTPResponse resp;
            istream &is = receiveResponse(resp);
            string strResp;
            Poco::StreamCopier::copyToString(is, strResp);
            cout << "response: " << strResp << endl;
            cout << ">>>>> END IFTTT WebHook Request" << endl;
            return resp.getStatus() == HTTPResponse::HTTPStatus::HTTP_OK;
        }
//curl -X POST --header "Content-Type: application/json" --data '{"value1":"off"}' https://maker.ifttt.com/trigger/smartthings_switch_2/with/key/duP4pgovijmo-Zhh184oZu && echo ""

    };
};
#endif