#ifndef __XMONIT_REQUEST__
#define __XMONIT_REQUEST__

#include <Poco/JSON/JSON.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Dynamic/Var.h>

#include <string>

using namespace std;

namespace xmonit {

  struct XmonitJsonMessage {
    virtual Poco::JSON::Object::Ptr toJson() const = 0;
    virtual Poco::JSON::Object::Ptr parseJson(istream& istream) = 0;  
    virtual Poco::JSON::Object::Ptr parseJson(const char* pszJson) {
      stringstream ss(pszJson);
      return parseJson(ss);      
    }  
  };

  struct XmonitResponse : public XmonitJsonMessage {
    std::string msg;
    int code;

    virtual Poco::JSON::Object::Ptr toJson() const override {
      Poco::JSON::Object::Ptr root = new Poco::JSON::Object();
      root->set("msg", msg);
      root->set("code", code);
      return root;
    }

    virtual Poco::JSON::Object::Ptr parseJson(istream& istr) override {
      Poco::JSON::Parser parser;
      Poco::Dynamic::Var result = parser.parse(istr);
      Poco::JSON::Object::Ptr jsonObj = result.extract<Poco::JSON::Object::Ptr>();
      msg = jsonObj->getValue<std::string>("msg");
      code = jsonObj->getValue<float>("code");
      return jsonObj;
    }
  };

  struct XmonitRequest : XmonitJsonMessage {
    std::string requestType;
    std::string requestMethod;
  
    XmonitRequest(const std::string& requestType, const std::string& requestMethod) :
      requestType(requestType),
      requestMethod(requestMethod){}

    virtual Poco::JSON::Object::Ptr toJson() const override {
      Poco::JSON::Object::Ptr root = new Poco::JSON::Object();
      root->set("requestType", requestType);
      root->set("requestMethod", requestMethod);
      return root;
    }

    virtual Poco::JSON::Object::Ptr parseJson(istream& istr) override {
      Poco::JSON::Parser parser;
      Poco::Dynamic::Var result = parser.parse(istr);
      Poco::JSON::Object::Ptr jsonObj = result.extract<Poco::JSON::Object::Ptr>();
      requestType = jsonObj->getValue<std::string>("requestType");
      requestMethod = jsonObj->getValue<std::string>("requestMethod");
      return jsonObj;
    }

    virtual std::string toString() const {
      return XmonitRequest::toString(toJson());
    }

    static std::string toString(Poco::JSON::Object::Ptr pObj, int indentSize = 2) {
      stringstream ss;
      pObj->stringify(ss,indentSize);
      return ss.str();
    }
  };

  struct DeviceCapabilityRequest : XmonitRequest {      
    std::string deviceNameFilter;
    std::string capabilityType;
    float capabilityValue;
        
    DeviceCapabilityRequest(const std::string& capabilityType,        
      float capabilityValue=0,
      const std::string& deviceNameFilter = "" ) : XmonitRequest("capability","update"), 
        capabilityType(capabilityType),
        capabilityValue(capabilityValue),
        deviceNameFilter(deviceNameFilter) {
    };

    virtual Poco::JSON::Object::Ptr toJson() const override {
      Poco::JSON::Object::Ptr root = XmonitRequest::toJson();
      root->set("deviceNameFilter", deviceNameFilter);
      root->set("capabilityType", capabilityType);
      root->set("capabilityValue", capabilityValue);
      return root;
    }

    virtual Poco::JSON::Object::Ptr parseJson(istream& istr) override {
      Poco::JSON::Object::Ptr jsonObj = XmonitRequest::parseJson(istr);
      deviceNameFilter = jsonObj->getValue<std::string>("deviceNameFilter");
      capabilityType = jsonObj->getValue<std::string>("capabilityType");
      capabilityValue = jsonObj->getValue<float>("capabilityValue");
      return jsonObj;    
    }

    virtual std::string toString() const override {
      stringstream ss;
      toJson()->stringify(ss,2);
      return ss.str();
    }
  };

  struct ToggleRequest : DeviceCapabilityRequest {
    ToggleRequest(bool bOn = true, std::string deviceNameFilter = "") : 
      DeviceCapabilityRequest("TOGGLE", bOn ? 1.0 : 0, deviceNameFilter) {
    }
  };

}
#endif