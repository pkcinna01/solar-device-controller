#include "HttpServer.h"
#include "SolarPowerMgrApp.h"
#include "automation/device/PowerSwitch.h"
#include "Poco/Mutex.h"
#include "Poco/StringTokenizer.h"
#include "Poco/NumberParser.h"
#include "Poco/URI.h"

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

    Poco::URI uri(req.getURI());
    //cout << __PRETTY_FUNCTION__ << " uri path: " << uri.getPath() << " method: " << req.getMethod() << endl;

    vector<string> vecPath;
    uri.getPathSegments(vecPath);
    string strMethod = Poco::toLower(req.getMethod());
    vector<string> vecAccept;
    HTTPServerRequest::splitElements(req.get("accept","application/json"),vecAccept);
    bool bTextPlainResp = find(vecAccept.begin(),vecAccept.end(),"text/plain") != vecAccept.end() 
                          && find(vecAccept.begin(),vecAccept.end(),"application/json") == vecAccept.end();
    string strContentType = bTextPlainResp ? "text/plain" : "application/json";

    Poco::URI::QueryParameters queryParams = uri.getQueryParameters();

    bool bVerbose = false;
    vector<string> fields;
    for( std::pair<string,string> param : queryParams ) {
      if ( Poco::toLower(param.first) == "verbose" ) {
        bVerbose = param.second.empty() || automation::text::parseBool(param.second.c_str());
      } else if ( Poco::toLower(param.first) == "fields" ) {
        Poco::StringTokenizer pathTokenizer(param.second,",",StringTokenizer::TOK_IGNORE_EMPTY|StringTokenizer::TOK_TRIM);
        for( string token : pathTokenizer ) {
          fields.push_back(token);
        }
      }
    }


    HTTPResponse::HTTPStatus respStatus(HTTPResponse::HTTP_OK);
    ostream &out = resp.send();
    
    try {

      Poco::JSON::Object::Ptr pReqObj;

      if ( strMethod == "put" || strMethod == "post") {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(req.stream());
        pReqObj = result.extract<Poco::JSON::Object::Ptr>();
      }

      Poco::Net::IPAddress clientAddress(req.clientAddress().host());
      
      if (find(allowedIpAddresses.begin(), allowedIpAddresses.end(), clientAddress) == allowedIpAddresses.end()) {
        
        cerr << __PRETTY_FUNCTION__ << " unauthorized origin: " << clientAddress.toString() <<endl;
        writeJsonResp(out, -1, string("Unauthorized request origin: ") + clientAddress.toString());
        respStatus = HTTPResponse::HTTP_UNAUTHORIZED;

      } else if (mutex.tryLock(10000)) {   
        try {   
          if ( vecPath.empty() ) {

            std::string strMsg("Rest endpoint required (device or app): ");
            strMsg += req.getURI();
            cerr << __PRETTY_FUNCTION__ << strMsg << endl;
            writeJsonResp( out, -8, strMsg );
            respStatus = HTTPResponse::HTTP_BAD_REQUEST;

          } else if ( vecPath[0] == "device") {

            Devices foundDevices;

            if ( vecPath.size() == 2 ) {

              string strId = vecPath[1];
              int id = NumberParser::parse(strId);
              SolarPowerMgrApp::pInstance->devices.findById(id,foundDevices);

            } else {

              Poco::DynamicAny deviceVar;
              if ( pReqObj ) {
                deviceVar = pReqObj->get("deviceName");
              }
              if ( !deviceVar.isEmpty() ) {
                SolarPowerMgrApp::pInstance->devices.findByTitleLike(deviceVar.toString().c_str(), foundDevices);              
              } else if ( strMethod == "get" ) {
                SolarPowerMgrApp::pInstance->devices.findByTitleLike("*", foundDevices);              
              }
            }

            if ( foundDevices.empty() ) {

              respStatus = HTTPResponse::HTTP_NOT_FOUND;
              string respMsg("No devices found. URI: ");
              respMsg += req.getURI();
              writeJsonResp(out, -1, respMsg);
              cerr << __PRETTY_FUNCTION__ << respMsg << endl;

            } else if ( strMethod == "get" ) {

              int respCode = 0;
              string respMsg;
              Poco::JSON::Object respObj(true);
              Poco::JSON::Array deviceArr;
              
              for ( int i = 0; i < foundDevices.size(); i++ ) {

                automation::PowerSwitch* pSwitch = dynamic_cast<automation::PowerSwitch*>(foundDevices[i]);

                if ( pSwitch ) {
                  json::StringStreamPrinter ssp;
                  json::JsonStreamWriter w(ssp);
                  pSwitch->print(w,bVerbose||!fields.empty(),false);
                  JSON::Parser parser;
                  Poco::Dynamic::Var deviceVar = parser.parse(ssp.ss); //TODO - Handle "nan" numbers
                  Poco::JSON::Object::Ptr pJson = deviceVar.extract<Poco::JSON::Object::Ptr>();
                  if ( bTextPlainResp ) {
                    if ( i > 0 ) {
                      out << endl;
                    }
                    for ( int fieldIndex = 0; fieldIndex < fields.size(); fieldIndex++ ) { 
                      if ( fieldIndex > 0 ) {
                        out << ",";
                      }
                      Poco::DynamicAny var = pJson->get(fields[fieldIndex]);
                      if ( !var.isEmpty() ) {
                        out << var.toString();
                      } else {
                        out << "NULL";
                      }
                    }
                  } else {
                    if ( fields.empty() ) {
                      deviceArr.add(pJson);
                    } else {
                      Poco::JSON::Object jObj(true);
                      fields.push_back("id");
                      fields.push_back("name");
                      for ( string field : fields) {
                        jObj.set(field,pJson->get(field));
                      }
                      deviceArr.add(jObj);
                    }
                  }
                }
              }

              if ( !bTextPlainResp ) {
                respObj.set("devices",deviceArr);
                writeJsonResp(out, respCode, respMsg, respObj);
              }

            } else if ( pReqObj ) { 

              int respCode = 0;
              string respMsg;
              Poco::JSON::Object respObj(true);
              Poco::JSON::Array deviceArr;

              string strKey = pReqObj->get("key");
              string strVal = pReqObj->get("value");

              for ( int i = 0; i < foundDevices.size(); i++ ) {
                automation::PowerSwitch* pSwitch = dynamic_cast<automation::PowerSwitch*>(foundDevices[i]);
                if ( pSwitch ) {
                  cout << "Setting device '" << pSwitch->name << "' " << strKey << "=" << strVal << endl;
                  stringstream ss;
                  automation::SetCode statusCode = pSwitch->setAttribute(strKey.c_str(), strVal.c_str(), &ss);
                  if ( !respMsg.empty() ) {
                    respMsg += " | ";
                  }
                  respMsg += ss.str();
                  if ( respCode == 0 ) { // don't set respCode back to zero if an error already occurred for prior device
                    respCode = (int)statusCode; 
                  }
                  json::StringStreamPrinter ssp;
                  json::JsonStreamWriter w(ssp);
                  pSwitch->print(w,false,false);
                  JSON::Parser parser;
                  Poco::Dynamic::Var deviceVar = parser.parse(ssp.ss); //TODO - Handle "nan" numbers
                  Poco::JSON::Object::Ptr deviceJson = deviceVar.extract<Poco::JSON::Object::Ptr>();
                  deviceJson->set("key",strKey);
                  deviceJson->set("value",strVal);
                  deviceJson->set("statusCode",(int)statusCode);
                  deviceArr.add(deviceJson);
                }
              }

              respObj.set("devices",deviceArr);
              writeJsonResp(out, respCode, respMsg, respObj);
            } else {
              respStatus = HTTPResponse::HTTP_BAD_REQUEST;
              out << "Invalid device http METHOD: '" << strMethod << "'. URI: " << req.getURI() << endl;
              cerr << __PRETTY_FUNCTION__ << " Invalid device METHOD: '" << strMethod << "'. URI: " << req.getURI() << endl;
            }
          } else if ( vecPath[0] == "app" ) {    
            if ( strMethod == "get" ) {
              if ( fields.empty() || fields.size() == 1 && Poco::toLower(fields[0]) == "enabled" ) {
                if ( bTextPlainResp ) { //openhab
                  string strVal =  SolarPowerMgrApp::pInstance->bEnabled  ? "TRUE" : "FALSE";
                  out << strVal;
                  out << flush;
                } else {
                  resp.setContentType("application/json");
                  Poco::JSON::Object respObj(true);
                  respObj.set("enabled", SolarPowerMgrApp::pInstance->bEnabled);
                  writeJsonResp(out, 0, "OK", respObj);
                }
              } else {
                throw Poco::Exception("Unsupported app field(s)");
              }
            } else if ( pReqObj ) {
              string strKey = pReqObj->get("key");
              strKey = Poco::toLower(strKey);
              string strVal = pReqObj->get("value");
              if ( strKey == "enabled" && !strVal.empty() ) {
                SolarPowerMgrApp::pInstance->bEnabled = automation::text::parseBool(strVal.c_str());
                writeJsonResp(out, 0, string("SolarPowerMgrApp ") + (SolarPowerMgrApp::pInstance->bEnabled?"ENABLED":"DISABLED"));
              } else {
                string respMsg("Unrecognized request format. key='");
                respMsg += strKey + "', value='";
                respMsg += strVal + "'";
                writeJsonResp(out, -4, respMsg);
                cout << __PRETTY_FUNCTION__ << " " << respMsg << endl;
              }
            } else {
              respStatus = HTTPResponse::HTTP_BAD_REQUEST;
              out << "Invalid app http METHOD: '" << strMethod << "'. URI: " << req.getURI() << endl;
              cerr << __PRETTY_FUNCTION__ << " Invalid app METHOD: '" << strMethod << "'. URI: " << req.getURI() << endl;
            }
          } else {
            std::string strMsg("Invalid endpoint: ");
            strMsg += req.getURI();
            cerr << __PRETTY_FUNCTION__ << strMsg << endl;
            writeJsonResp( out, -9, strMsg );
            respStatus = HTTPResponse::HTTP_BAD_REQUEST;
          }
          mutex.unlock();
        } catch( const Poco::Exception &ex ) {
          mutex.unlock();
          throw ex;
        }
      } else {
        std::string strMsg = "ERROR: Timed out waiting for main application to update a device.  Skipping HTTP request: ";
        strMsg += req.getURI();
        cerr << __PRETTY_FUNCTION__ << strMsg << endl;
        writeJsonResp( out, -10, strMsg );
        respStatus = HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
      }
      out << flush;
    } catch (const Poco::Exception &ex) {
      cerr << __PRETTY_FUNCTION__ << " " << ex.displayText() << " URI:" << req.getURI() << endl;
      if ( bTextPlainResp ) {
        out << ex.displayText();
      } else {
        writeJsonResp(out,-999,ex.displayText());
      }
      respStatus = HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;    
    }
    resp.setContentType(strContentType);
    resp.setStatus(respStatus);
  }

  void DefaultRequestHandler::writeJsonResp(ostream& out, int statusCode, const string& statusMsg, Poco::JSON::Object& obj) {
    obj.set("respCode", statusCode);
    obj.set("respMsg", statusMsg);
    obj.stringify(out, 2);
    out << endl;
  }

  void DefaultRequestHandler::writeJsonResp(ostream& out, int statusCode, const string& statusMsg) {
    Poco::JSON::Object obj(true);  
    writeJsonResp(out,statusCode,statusMsg,obj);
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
