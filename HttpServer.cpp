#include "HttpServer.h"
#include "SolarPowerMgrApp.h"
#include "automation/device/PowerSwitch.h"
#include "Poco/Mutex.h"
#include "Poco/StringTokenizer.h"
#include "Poco/JSON/Query.h"
#include "Poco/NumberParser.h"
#include "Poco/URI.h"
#include <Poco/RegularExpression.h>

namespace xmonit
{
  const Poco::RegularExpression SINGLE_FIELD_QUERY_RE("^(on|constraint[.]enabled)$", Poco::RegularExpression::RE_CASELESS, true);

  HTTPRequestHandler *RequestHandlerFactory::createRequestHandler(const HTTPServerRequest &)
  {
    DefaultRequestHandler *pReqHandler = new DefaultRequestHandler(mutex, allowedIpAddresses);
    return pReqHandler;
  }

  void DefaultRequestHandler::handleRequest(HTTPServerRequest &req, HTTPServerResponse &resp)
  {
    //cout << __PRETTY_FUNCTION__ << "[" << std::this_thread::get_id() << "] begin " << req.getURI() << endl;

    Poco::Mutex::ScopedLock lock(mutex);

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
    string nameParam;

    for( std::pair<string,string> param : queryParams ) {
      if ( Poco::toLower(param.first) == "verbose" ) {
        bVerbose = param.second.empty() || automation::text::parseBool(param.second.c_str());
      } else if ( Poco::toLower(param.first) == "fields" ) {
        Poco::StringTokenizer pathTokenizer(Poco::toLower(param.second),",",StringTokenizer::TOK_IGNORE_EMPTY|StringTokenizer::TOK_TRIM);
        for( string token : pathTokenizer ) {
          fields.push_back(token);
        }
      } else if ( Poco::toLower(param.first) == "name" ) {
        nameParam = param.second;
      }
    }

    HTTPResponse::HTTPStatus respStatus(HTTPResponse::HTTP_OK);
    ostream &out = resp.send();
    
    try {

      //cerr << __PRETTY_FUNCTION__ << "[" << std::this_thread::get_id() << "] begin main try block" << endl;
      Poco::JSON::Object::Ptr pReqObj;

      if ( strMethod == "put" || strMethod == "post") {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(req.stream());
        pReqObj = result.extract<Poco::JSON::Object::Ptr>();
      } else if ( vecPath.size() == 1 && !nameParam.empty() ) {
        // a GET request with "name" parameter (wildcard search by title)
        pReqObj = new Poco::JSON::Object();
        pReqObj->set("name",nameParam);
      }

      Poco::Net::IPAddress clientAddress(req.clientAddress().host());
      
      if (find(allowedIpAddresses.begin(), allowedIpAddresses.end(), clientAddress) == allowedIpAddresses.end()) {
        
        cerr << __PRETTY_FUNCTION__ << " unauthorized origin: " << clientAddress.toString() <<endl;
        writeJsonResp(out, -1, string("Unauthorized request origin: ") + clientAddress.toString());
        respStatus = HTTPResponse::HTTP_UNAUTHORIZED;

      } else {  
        if ( vecPath.empty() ) {

          std::string strMsg("Rest endpoint required (device, constraint, sensor, capability, or app): ");
          strMsg += req.getURI();
          cerr << __PRETTY_FUNCTION__ << strMsg << endl;
          writeJsonResp( out, -8, strMsg );
          respStatus = HTTPResponse::HTTP_BAD_REQUEST;

        } else if ( vecPath[0] == "device" || vecPath[0] == "constraint" || vecPath[0] == "sensor" || vecPath[0] == "capability" ) {
          string itemType = vecPath[0];
          AttributeContainerVector<AttributeContainer*> foundVec;
          AttributeContainerVector<AttributeContainer*> srcVec;
          if ( itemType == "device" ) {
            srcVec.insert(srcVec.begin(), SolarPowerMgrApp::pInstance->devices.begin(), SolarPowerMgrApp::pInstance->devices.end());
          } else if ( itemType == "constraint" ) {
            srcVec.insert(srcVec.begin(), Constraint::all().begin(), Constraint::all().end());
          } else if ( itemType == "capability" ) {
            srcVec.insert(srcVec.begin(), Capability::all().begin(), Capability::all().end());
          } else if ( itemType == "sensor" ) {
            srcVec.insert(srcVec.begin(), SolarPowerMgrApp::pInstance->sensors.begin(), SolarPowerMgrApp::pInstance->sensors.end());
          } else {
            throw Poco::Exception("Unsupported rest item type");
          }

          if ( vecPath.size() == 2 ) {

            string strId = vecPath[1];
            int id = NumberParser::parse(strId);
            srcVec.findById(id,foundVec);

          } else {

            Poco::DynamicAny itemVar;
            if ( pReqObj ) {
              itemVar = pReqObj->get("name");
            }
            if ( !itemVar.isEmpty() ) {
              srcVec.findByTitleLike(itemVar.toString().c_str(), foundVec);              
            } else if ( strMethod == "get" ) {
              srcVec.findByTitleLike("*", foundVec);              
            }
          }
          //cerr << __PRETTY_FUNCTION__ << "[" << std::this_thread::get_id() << "] device cnt: " << foundDevices.size() << endl; 

          if ( foundVec.empty() ) {

            respStatus = HTTPResponse::HTTP_NOT_FOUND;
            string respMsg("No items matched query. URI: ");
            respMsg += req.getURI();
            writeJsonResp(out, -1, respMsg);
            cerr << __PRETTY_FUNCTION__ << respMsg << endl;

          } else if ( strMethod == "get" ) {

            int respCode = 0;
            string respMsg;
            Poco::JSON::Object respObj(true);
            Poco::JSON::Array itemArr;

            if ( bTextPlainResp && fields.empty() && itemType == "device" ) {
              fields.push_back("on");
            }

            for ( int i = 0; i < foundVec.size(); i++ ) {

              AttributeContainer* pItem = foundVec[i];
              //if ( pSwitch ) {
              {
                // first see if its a simple query from openhab so we can skip expensive JSON parsing and query
                automation::PowerSwitch* pSwitch = nullptr;

                if ( bTextPlainResp && fields.size() == 1 
                     && SINGLE_FIELD_QUERY_RE.match(fields[0]) 
                     && (pSwitch=dynamic_cast<automation::PowerSwitch*>(pItem)) ) {

                  const string& field = fields[0];

                  if ( field == "on" ) {
                    out << pSwitch->isOn();
                  } else if ( field == "constraint.enabled" ) {
                    Constraint* pConstraint = pSwitch->getConstraint();
                    out << (pConstraint && pConstraint->bEnabled);
                  } else {
                    respStatus = HTTPResponse::HTTP_BAD_REQUEST;
                    cerr << __PRETTY_FUNCTION__ << " Device GET request expected text field in (on|enabled) but found: '" << field << "'" << endl;
                  }
                } else {
                  // This may access device constraints or sensors which are also used in several places in main thread
                  json::StringStreamPrinter ssp;
                  json::JsonStreamWriter w(ssp);
                  pItem->print(w,bVerbose||!fields.empty(),false);
                  JSON::Parser parser;

                  try {
                    Poco::Dynamic::Var itemVar = parser.parse(ssp.ss); //TODO - Handle "nan" numbers
                    Poco::JSON::Object::Ptr pJson = itemVar.extract<Poco::JSON::Object::Ptr>();
                    if ( bTextPlainResp ) {
                      if ( i > 0 ) {
                        out << endl;
                      }
                      Poco::JSON::Query query(pJson);      
                      for ( int fieldIndex = 0; fieldIndex < fields.size(); fieldIndex++ ) { 
                        if ( fieldIndex > 0 ) {
                          out << ",";
                        }
                        Poco::DynamicAny var = query.find(fields[fieldIndex]);
                        if ( !var.isEmpty() ) {
                          out << var.toString();
                        } else {
                          out << "NULL";
                        }
                      }
                    } else {
                      if ( fields.empty() ) {
                        itemArr.add(pJson);
                      } else {
                        Poco::JSON::Object jObj;
                        fields.push_back("id");
                        if ( itemType == "constraint" ) {
                          fields.push_back("title");
                        } else {
                          fields.push_back("name");
                        }
                        for ( string field : fields) {
                          jObj.set(field,Poco::JSON::Query(pJson).find(field));
                        }
                        itemArr.add(jObj);
                      }
                    }
                  } catch (const Poco::Exception &ex) {
                    cerr << __PRETTY_FUNCTION__ << "Failed extrating JSON data from: " << endl;
                    cerr << ssp.ss.str() << endl;
                    ex.rethrow();
                  }
                }
              }
            }

            if ( !bTextPlainResp ) {
              respObj.set(itemType,itemArr);
              writeJsonResp(out, respCode, respMsg, respObj);
            }

          } else if ( pReqObj ) { 

            int respCode = 0;
            string respMsg;
            Poco::JSON::Object respObj(true);
            Poco::JSON::Array itemArr;

            string strKey = pReqObj->get("key");
            string strVal = pReqObj->get("value");

            for ( int i = 0; i < foundVec.size(); i++ ) {

              AttributeContainer* pItem = foundVec[i];
              cout << "Setting " << itemType << " '" << pItem->getTitle() << "' " << strKey << "=" << strVal << endl;
              stringstream ss;
              automation::SetCode statusCode = pItem->setAttribute(strKey.c_str(), strVal.c_str(), &ss);
              if ( !respMsg.empty() ) {
                respMsg += " | ";
              }
              respMsg += ss.str();
              if ( respCode == 0 ) { // don't set respCode back to zero if an error already occurred for prior item
                respCode = (int)statusCode; 
              }
              json::StringStreamPrinter ssp;
              json::JsonStreamWriter w(ssp);
              pItem->print(w,true);
              JSON::Parser parser;
              Poco::Dynamic::Var itemVar = parser.parse(ssp.ss); //TODO - Handle "nan" numbers
              Poco::JSON::Object::Ptr itemJson = itemVar.extract<Poco::JSON::Object::Ptr>();
              Poco::DynamicStruct rtnJson;
              rtnJson.insert("id",pItem->id);
              rtnJson.insert("name",pItem->getTitle());
              rtnJson.insert("key",strKey);
              rtnJson.insert("value",Poco::JSON::Query(itemJson).find(strKey));
              rtnJson.insert("statusCode",(int)statusCode);
              itemArr.add(rtnJson);
            }

            if ( respCode != 0 && respMsg.empty() ) {
              respMsg = (respCode == (int)SetCode::Ignored) ? "Key not found" : "ERROR";
            }
            respObj.set(itemType,itemArr);
            writeJsonResp(out, respCode, respMsg, respObj);
          } else {
            respStatus = HTTPResponse::HTTP_BAD_REQUEST;
            out << "Invalid " << itemType << " http METHOD: '" << strMethod << "'. URI: " << req.getURI() << endl;
            cerr << __PRETTY_FUNCTION__ << " Invalid " << itemType << " METHOD: '" << strMethod << "'. URI: " << req.getURI() << endl;
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
      }
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
    //cerr << __PRETTY_FUNCTION__ << "[" << std::this_thread::get_id() << "] end" << endl;
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
