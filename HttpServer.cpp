#include "HttpServer.h"
#include "SolarPowerMgrApp.h"
#include "automation/device/PowerSwitch.h"
#include "Poco/Mutex.h"
#include "Poco/StringTokenizer.h"
#include "Poco/NumberParser.h"

namespace xmonit
{

HTTPRequestHandler *RequestHandlerFactory::createRequestHandler(const HTTPServerRequest &)
{
  DefaultRequestHandler *pReqHandler = new DefaultRequestHandler(mutex, allowedIpAddresses);
  return pReqHandler;
}

void DefaultRequestHandler::handleRequest(HTTPServerRequest &req, HTTPServerResponse &resp)
{
  client::watchdog::messageReceived();
  //cout << __PRETTY_FUNCTION__ << req.getURI() << endl;
  if ( req.getURI().find("/rest") == 0 ) {
    handleJsonRequest(req,resp);
  } else {
    resp.setContentType("text/plain");
    try {
      Poco::StringTokenizer pathTokenizer(req.getURI(),"/",StringTokenizer::TOK_IGNORE_EMPTY|StringTokenizer::TOK_TRIM);
      ostream &out = resp.send();
      int tokenCnt = pathTokenizer.count();
      if (mutex.tryLock(30000)) {
        if ( req.getMethod() == HTTPRequest::HTTP_GET ) {
          if ( tokenCnt == 2 ) {
            string cmd = pathTokenizer[0];
            string strId = pathTokenizer[1];
              int id = NumberParser::parse(strId);
              if ( cmd == "isOn" ) {
                Devices foundDevices;
                SolarPowerMgrApp::pInstance->devices.findById(id,foundDevices);
                if ( !foundDevices.empty() ) {
                  automation::PowerSwitch* pSwitch = dynamic_cast<automation::PowerSwitch*>(foundDevices[0]);
                  if ( pSwitch ) {
                    string strVal = pSwitch->isOn() ? "TRUE" : "FALSE";
                    out << strVal;
                    out << flush;
                  }
                }
              } else {
                resp.setStatus(HTTPResponse::HTTP_BAD_REQUEST);
                out << "Invalid request URL: " << req.getURI() << endl;
                cerr << __PRETTY_FUNCTION__ << " Invalid request URL: " << req.getURI() << endl;
              }
          } else if ( tokenCnt == 1 ) {
            // checking application state (no device id in url)
            string cmd = pathTokenizer[0];
            if ( cmd == "isEnabled" ) {
              string strVal =  SolarPowerMgrApp::pInstance->bEnabled  ? "TRUE" : "FALSE";
              out << strVal;
              out << flush;
            } else {
              resp.setStatus(HTTPResponse::HTTP_BAD_REQUEST);
              out << "Invalid request URL: " << req.getURI() << endl;
              cerr << __PRETTY_FUNCTION__ << " Invalid request URL: " << req.getURI() << endl;
            }
          }
        }
        mutex.unlock();
      }
    } catch (const Poco::Exception &ex) {
      cerr << __PRETTY_FUNCTION__ << " " << ex.displayText() << endl;
    }
  }
}

void DefaultRequestHandler::handleJsonRequest(HTTPServerRequest &req, HTTPServerResponse &resp)
{
    HTTPResponse::HTTPStatus respStatus(HTTPResponse::HTTP_OK);
    resp.setContentType("application/json");

    Poco::JSON::Parser parser;
    Poco::Dynamic::Var result = parser.parse(req.stream());
    int respCode = 0;
    string respMsg = "OK";

    Poco::JSON::Object::Ptr pReqObj = result.extract<Poco::JSON::Object::Ptr>();
    ostream &out = resp.send();
    Poco::JSON::Object respObj(true);
    //Poco::Dynamic::Var id = pReqObj->get("id");
    Poco::Dynamic::Var cmd = pReqObj->get("cmd");
    Poco::Net::IPAddress clientAddress(req.clientAddress().host());
    if (find(allowedIpAddresses.begin(), allowedIpAddresses.end(), clientAddress) == allowedIpAddresses.end())
    {
      respObj.set("respCode", -1);
      respObj.set("respMsg", string("Unauthorized request origin: ") + clientAddress.toString());
      respStatus = HTTPResponse::HTTP_UNAUTHORIZED;
    }
    else if (/*id.isEmpty() ||*/ cmd.isEmpty())
    {
      respObj.set("respCode", -1);
      respObj.set("respMsg", "Invalid request format");
      respStatus = HTTPResponse::HTTP_BAD_REQUEST;
    }
    else if (mutex.tryLock(30000))
    {
      try
      {
        if (cmd == "set")
        {
          Devices foundDevices;
          Poco::Dynamic::Var deviceVar = pReqObj->get("deviceId");
          if ( !deviceVar.isEmpty() ) {
            int deviceId = deviceVar.convert<int>();
            SolarPowerMgrApp::pInstance->devices.findById(deviceId, foundDevices);
          } else {
            deviceVar = pReqObj->get("deviceName");
            if ( !deviceVar.isEmpty() ) {
              SolarPowerMgrApp::pInstance->devices.findByTitleLike(deviceVar.toString().c_str(), foundDevices);
            } else {
              // setting something global on power manager app (not a device)
              string strKey = pReqObj->get("key");
              strKey = Poco::toLower(strKey);
              string strVal = pReqObj->get("value");
              if ( strKey == "enabled" && !strVal.empty() ) {
                SolarPowerMgrApp::pInstance->bEnabled = automation::text::parseBool(strVal.c_str());
                respMsg = "SolarPowerMgrApp ";
                respMsg += (SolarPowerMgrApp::pInstance->bEnabled?"ENABLED":"DISABLED");
              } else {
                respMsg = "Unrecognized request format. key='";
                respMsg += strKey + "', value='";
                respMsg += strVal + "'";
                respCode = -4;
              }
              cout << __PRETTY_FUNCTION__ << " " << respMsg << endl;
            }
          }
          if ( !deviceVar.isEmpty() ) {
            if (foundDevices.empty())
            {
              respCode = -3;
              respMsg = "'set' command matched zero devices for deviceId or deviceName";
            }
            else
            {
              respMsg = "";
              for ( Device *pDevice : foundDevices ) {
                string strKey = pReqObj->get("key");
                string strVal = pReqObj->get("value");
                cout << "Setting device '" << pDevice->name << "' " << strKey << "=" << strVal << endl;
                stringstream ss;
                automation::SetCode statusCode = pDevice->setAttribute(strKey.c_str(), strVal.c_str(), &ss);
                if ( !respMsg.empty() ) {
                  respMsg += " | ";
                }
                respMsg += ss.str();
                if ( respCode == 0 ) { // don't set respCode back to zero if an error already occurred for prior device
                  respCode = (int)statusCode; 
                }
              }
            }
          }
        }
        else if (cmd == "get")
        {
          //ex: curl --header "Content-Type: application/json" --request POST --data '{"cmd":"get", "deviceName":"Plant*", "verbose":true}' http://localhost:8096/rest
          json::StringStreamPrinter ssp;
          json::JsonStreamWriter w(ssp);
          bool bVerbose = !pReqObj->get("verbose").isEmpty() && automation::text::parseBool(pReqObj->get("verbose").toString().c_str());
          Devices foundDevices;
          Poco::Dynamic::Var deviceVar = pReqObj->get("deviceId");
          if ( !deviceVar.isEmpty() ) {
            int deviceId = deviceVar.convert<int>();
            SolarPowerMgrApp::pInstance->devices.findById(deviceId, foundDevices);
          } else {
            deviceVar = pReqObj->get("deviceName");
            SolarPowerMgrApp::pInstance->devices.findByTitleLike(deviceVar.isEmpty() ? "*" : deviceVar.toString().c_str(), foundDevices);
          }
          if ( !deviceVar.isEmpty() ) {
            w.printVector(foundDevices,"", bVerbose);
            JSON::Parser parser;
            Poco::Dynamic::Var devicesVar = parser.parse(ssp.ss); //TODO - Handle "nan" numbers
            respObj.set("devices", devicesVar);
          } else {
            string strKey = pReqObj->get("key");
            strKey = Poco::toLower(strKey);
            if ( strKey == "enabled" ) {
              respObj.set(strKey,SolarPowerMgrApp::pInstance->bEnabled);
            } else {
              respMsg = "Unrecognized GET format. key='";
              respMsg += strKey + "'";
              respCode = -5;
            }
          }
        }
      }
      catch (const Poco::Exception &ex)
      {
        cerr << "ERROR: " << __PRETTY_FUNCTION__ << " Poco::Exception occured" << endl;
        respMsg = ex.displayText();
        respCode = (int)-999;
      }
      //respObj.set("id", id.toString());
      respObj.set("respCode", respCode);
      respObj.set("respMsg", respMsg);
      resp.setStatus(respStatus);
      mutex.unlock();
    }
    else
    {
      std::string strMsg = "ERROR: Timed out waiting for main application to update a device.  Skipping HTTP request: ";
      strMsg + req.getURI();
      cerr << strMsg << endl;
      respObj.set("respCode", -10);
      respObj.set("respMsg", strMsg);
      resp.setStatus(HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
    }
    //cout << __PRETTY_FUNCTION__ << req.getURI() << endl;
    respObj.stringify(out, 2);
    out << endl;
    out.flush();
  }

  void HttpServer::init( Poco::Util::AbstractConfiguration& conf)
  {
    int port = conf.getInt("httpListener[@port]");

    std::vector<Poco::Net::IPAddress> allowedIpAddresses;  
    std::string strIp = conf.getString("httpListener.allowedHosts.host[0]","");
    int i = 0;
    while ( !strIp.empty() ) {
      allowedIpAddresses.push_back(Poco::Net::IPAddress(strIp));
      stringstream ss;
      ss << "httpListener.allowedHosts.host[" << ++i << "]";
      std::string strKey(ss.str());
      strIp = conf.getString(strKey,"");
    }
    cout << "Listen port: " << port << endl;
    for( auto& ipAddr : allowedIpAddresses ) {
      cout << "Allowed Origin: " << ipAddr.toString() << endl;
    }
    pHttpServerImpl = unique_ptr<HTTPServer>(
      new HTTPServer(
        new RequestHandlerFactory(mutex,allowedIpAddresses), 
        ServerSocket(port), 
        new HTTPServerParams
      )
    );
  }

} // namespace xmonit
