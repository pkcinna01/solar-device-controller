
#include "OneWireTherm.h"

#include "Poco/Glob.h"
#include "Poco/StreamCopier.h"
#include "Poco/Exception.h"
#include "Poco/NumberParser.h"
#include "Poco/Path.h"
#include "Poco/FileStream.h"
#include "Poco/Util/LayeredConfiguration.h"

#include <fstream>

namespace xmonit {

float OneWireThermSensor::getValueImpl() const {

  //cout << __PRETTY_FUNCTION__ << " name=" << name << ", fileName=" << filePath << ", title=" << title << endl;

  float fTempCelcius(0);
  std::string line;

  try {
    stringstream sstr("[one-wire name='");
    sstr << name << "' path='" << filePath << "'] ERROR:";
    ifstream fis(filePath, std::ios::in);
    if ( !std::getline(fis,line) ) {
      sstr << "Failed reading first line";
      throw Poco::Exception(sstr.str());
    } else if ( false ) {
      sstr << "Expected \".* YES\" on first line";
      throw Poco::Exception(sstr.str());
    } else if ( !std::getline(fis,line) ) {
      sstr << "Failed reading second line";
      throw Poco::Exception(sstr.str());
    } else if ( false ) {
      sstr << "Expected \".* t=[0-9]{5}$\" on second line";
      throw Poco::Exception(sstr.str());
    } else {
      int iTemperatureData = Poco::NumberParser::parse(line.substr(line.size()-5));
      fTempCelcius = iTemperatureData / 1000.0;
    }
  } catch ( const Poco::Exception& ex ) {
    cerr << __PRETTY_FUNCTION__ << " Exception: " << ex.displayText() << endl;
    fTempCelcius = NAN;
  } catch ( ... ) {
    cerr << __PRETTY_FUNCTION__ << " Exception occured" << endl;
    fTempCelcius = NAN;
  }

  return fTempCelcius;
}

void OneWireThermSensor::createMatching(vector<unique_ptr<OneWireThermSensor>>& sensors, Poco::Util::LayeredConfiguration& conf) {
      
  std::string filter = conf.getString("oneWire[@filter]", "28-*");
  std::string path = conf.getString("oneWire[@path]", "/sys/bus/w1/devices");
  std::string search = path + "/" + filter + "/w1_slave";
  unsigned long maxCacheAgeMs = conf.getDouble("oneWire.maxCacheAgeMs",60000);
  std::set<string> fileNames;
  Poco::Glob::glob(search.c_str(),fileNames);
  
  for( string fileName : fileNames ) {
      string name = Poco::Path(fileName).parent().makeFile().getFileName();
      string xpath = "oneWire.titles.title[@id='";
      xpath += name;
      xpath += "'][@value]";
      string title = conf.getString( xpath, name);
      cout << __PRETTY_FUNCTION__ << " name=" << name << ", fileName=" << fileName << ", title=" << title << ", xpath=" << xpath << endl;
      sensors.push_back( make_unique<OneWireThermSensor>(name,fileName,title, maxCacheAgeMs));
  }
}

}