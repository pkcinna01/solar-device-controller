#ifndef SOLAR_IFTTT_PROMETHEUS_H
#define SOLAR_IFTTT_PROMETHEUS_H

#include "automation/sensor/Sensor.h"

#include <Poco/URI.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/RegularExpression.h>
#include <Poco/NumberParser.h>
#include <Poco/Exception.h>

#include <string>
#include <map>
#include <unordered_map>
#include <iostream>
#include <functional>

using namespace Poco;
using namespace std;

namespace Prometheus
{

const RegularExpression METRIC_RE("^(\\w+)([{](.*?),?[}])?\\s+(.*)", 0, true);
const RegularExpression METRIC_ATTRIBS_RE("^\\s*(\\w+)\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*,?\\s*", 0, true);

struct Metric
{
  string name;
  float value;
  map<string, string> attributes;
};

class MetricVector : public vector<Metric>
{
public:
  float avg()
  {
    float avg = 0;
    for (auto const &metric : *this) {
      if ( isnan(metric.value) ) {
        return metric.value;
      }
      avg += metric.value;
    }
    avg = avg / size();
    return avg;
  }

  float total()
  {
    float total = 0;
    for (auto const &metric : *this) {
      if ( isnan(metric.value) ) {
        return metric.value;
      }
      total += metric.value;
    }
    return total;
  }

  friend std::ostream &operator<<(std::ostream &os, const MetricVector &metrics)
  {
    for (auto metric : metrics)
    {
      os << "\t"
         << "{";
      for (auto attr : metric.attributes)
      {
        os << attr.first << ": " << attr.second << ", ";
      }
      os << "} " << metric.value << endl;
    }
    return os;
  }
};

class MetricMap : public unordered_map<string, MetricVector>
{

  friend std::ostream &operator<<(std::ostream &os, const MetricMap &mm)
  {
    for (auto metricMapEntry : mm)
    {
      string metricName = metricMapEntry.first;
      os << metricName << ": " << endl;
      MetricVector metrics = metricMapEntry.second;
      os << metrics;
    }
    return os;
  }
};


class DataSource
{
public:
  Poco::URI url;

  Prometheus::MetricMap metrics;
  Poco::Net::HTTPClientSession session;
  string path;
  function<bool(const Metric &)> metricFilter;

  DataSource(const Poco::URI &url, function<bool(const Metric &)> metricFilter) : url(url),
                                                                                  metricFilter(metricFilter),
                                                                                  session(url.getHost(), url.getPort()),
                                                                                  path(url.getPathAndQuery())
  {
    if (path.empty())
      path = "/";
  }

  bool parseMetrics(istream &inputStream, MetricMap &metricsMap, function<bool(const Metric &)> &metricFilter)
  {
    for (string line; getline(inputStream, line);)
    {
      if (Prometheus::METRIC_RE.match(line))
      {
        vector<string> captures;
        Prometheus::METRIC_RE.split(line, captures);
        string strKey(captures[1]);
        string strAttributes(captures[3]);
        string strVal(captures[4]);
        //cout << ">>>" << strKey << "{" << strAttributes << "} " << strVal << endl;
        double value = 0;
        if (NumberParser::tryParseFloat(strVal, value))
        {
          RegularExpression::MatchVec matches;
          Prometheus::Metric metric;
          metric.name = strKey;
          metric.value = value;
          while (Prometheus::METRIC_ATTRIBS_RE.split(strAttributes, captures))
          {
            strAttributes = strAttributes.substr(captures[0].length());
            metric.attributes[captures[1]] = captures[2];
          }
          if (metricFilter(metric))
          {
            metricsMap[strKey].push_back(metric);
          }
        }
      }
      else if (!line.empty() && line[0] != '#')
      {
        std::cerr << "Failed parsing Prometheus record: " << line << endl;
        return false;
      }
    }
    return true;
  }

  virtual bool loadMetrics()
  {
    metrics.clear();
    try
    {
      Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, path, Poco::Net::HTTPMessage::HTTP_1_1);
      session.sendRequest(request);

      Poco::Net::HTTPResponse response;
      std::istream &rs = session.receiveResponse(response);

      if (response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK)
      {
        if (!parseMetrics(rs, metrics, metricFilter))
        {
          cerr << "FAILED parsing metrics." << endl;
          automation::sleep(5000);
          return false;
        }
      }
      else
      {
        cerr << "FAILED retrieving metrics.  URL: " << url.toString() << ", reason: " << Poco::Net::HTTPResponse::getReasonForStatus(response.getStatus()) << endl;
        automation::sleep(5000);
        return false;
      }
    }
    catch (Poco::Exception &ex)
    {
      cerr << "FAILED loading prometheus metrics." << endl;
      cerr << ex.displayText() << endl;
    }
    return true;
  }

  friend std::ostream &operator<<(std::ostream &os, const DataSource &ds)
  {
    os << "DataSource{ URL:" << ds.url.toString() << endl;
    os << ds.metrics;
    os << "}";
    return os;
  }
};

}; // namespace Prometheus

#endif //SOLAR_IFTTT_PROMETHEUS_H
