#include "HttpServer.h"
#include "DeviceControllerApp.h"

#include "Poco/Mutex.h"

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

  if (mutex.tryLock(30000))
  {
    resp.setContentType("application/json");

    Poco::JSON::Parser parser;
    Poco::Dynamic::Var result = parser.parse(req.stream());
    int respCode = 0;
    string respMsg = "OK";
    HTTPResponse::HTTPStatus respStatus(HTTPResponse::HTTP_OK);

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
    else
    {
      try
      {
        if (cmd == "set")
        {
          Devices foundDevices;
          Poco::Dynamic::Var var = pReqObj->get("deviceId");
          if ( !var.isEmpty() ) {
            int deviceId = var.convert<int>();
            DeviceControllerApp::pInstance->devices.findById(deviceId, foundDevices);
          } else {
            var = pReqObj->get("deviceName");
            if ( !var.isEmpty() ) {
              DeviceControllerApp::pInstance->devices.findByTitleLike(var.toString().c_str(), foundDevices);
            } 
          }
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
        else if (cmd == "get")
        {
          json::StringStreamPrinter ssp;
          json::JsonStreamWriter w(ssp);
          bool bVerbose = !pReqObj->get("verbose").isEmpty() && automation::text::parseBool(pReqObj->get("verbose").toString().c_str());
          Devices foundDevices;
          Poco::Dynamic::Var devIdVar = pReqObj->get("deviceId");
          if ( !devIdVar.isEmpty() ) {
            int deviceId = devIdVar.convert<int>();
            DeviceControllerApp::pInstance->devices.findById(deviceId, foundDevices);
          } else {
            Poco::Dynamic::Var devNameVar = pReqObj->get("deviceName");
            DeviceControllerApp::pInstance->devices.findByTitleLike(devNameVar.isEmpty() ? "*" : devNameVar.toString().c_str(), foundDevices);
          }
          w.printVector(foundDevices,"", bVerbose);
          JSON::Parser parser;
          Poco::Dynamic::Var devicesVar = parser.parse(ssp.ss);
          respObj.set("devices", devicesVar);
        }
      }
      catch (Poco::Exception &ex)
      {
        cerr << "ERROR: " << __PRETTY_FUNCTION__ << " Poco::Exception occured" << endl;
        respMsg = ex.displayText();
        respCode = (int)-999;
      }
      //respObj.set("id", id.toString());
      respObj.set("respCode", respCode);
      respObj.set("respMsg", respMsg);
      resp.setStatus(respStatus);
    }

    mutex.unlock();
    respObj.stringify(out, 2);
    out << endl;
    out.flush();
  }
  else
  {
    cerr << "ERROR: Timed out waiting for main application to update a device.  Skipping HTTP request: " << req.getURI() << endl;
  }
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
