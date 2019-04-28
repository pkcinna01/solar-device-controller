#ifndef SOLAR_IFTTT_HTTP_SERVER_H
#define SOLAR_IFTTT_HTTP_SERVER_H


#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/DynamicStruct.h>
#include <Poco/JSON/Object.h>
#include "Poco/JSON/Parser.h"
#include "Poco/JSON/ParseHandler.h"
#include "Poco/JSON/Stringifier.h"
#include "Poco/Mutex.h"

#include <iostream>
#include <string>
#include <vector>

using namespace Poco::Net;
using namespace Poco::Util;
using namespace Poco;
using namespace std;

namespace xmonit {

class DefaultRequestHandler : public HTTPRequestHandler
{
  Mutex& mutex;
  std::vector<Poco::Net::IPAddress>& allowedIpAddresses;
public:
  DefaultRequestHandler(Mutex& mutex, std::vector<Poco::Net::IPAddress>& allowedIpAddresses) : mutex(mutex), allowedIpAddresses(allowedIpAddresses) {
    
  }

  virtual void handleRequest(HTTPServerRequest &req, HTTPServerResponse &resp);
};

class RequestHandlerFactory : public HTTPRequestHandlerFactory
{
public:
  Mutex& mutex;
  std::vector<Poco::Net::IPAddress> allowedIpAddresses;

  RequestHandlerFactory(Mutex& mutex,std::vector<Poco::Net::IPAddress>& allowedIpAddresses) : mutex(mutex), allowedIpAddresses(allowedIpAddresses) {

  }

  virtual HTTPRequestHandler* createRequestHandler(const HTTPServerRequest &);
};

class HttpServer  {

protected:
std::unique_ptr<HTTPServer> pHttpServerImpl;

public:
  Mutex mutex;

  void init( Poco::Util::AbstractConfiguration& conf);
  
  void start() {
    pHttpServerImpl->start();
  }

  void stop() {
    pHttpServerImpl->stop();
  }

};
}
#endif