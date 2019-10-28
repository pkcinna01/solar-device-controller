
#include "OneWireTherm.h"

#include "Poco/Glob.h"
#include "Poco/StreamCopier.h"
#include "Poco/Exception.h"
#include "Poco/NumberParser.h"
#include "Poco/Path.h"
#include "Poco/FileStream.h"
#include "Poco/Util/LayeredConfiguration.h"
#include "Poco/RegularExpression.h"

#include <fstream>

namespace xmonit {

float OneWireThermSensor::getValueImpl() const {

  //cout << __PRETTY_FUNCTION__ << " name=" << name << ", fileName=" << filePath << ", title=" << title << endl;

  float fTempCelcius(0);
  std::string line;

  auto errorFormatter = [this](const string& msg) { 
    stringstream sstr;
    sstr << "ERROR[one-wire name='" << name << "' path='" << filePath << "']: " << msg; 
    return sstr.str();
  };

  try {    
    
    ifstream fis(filePath, std::ios::in);
    const static Poco::RegularExpression HAS_YES_SUFFIX(".*YES$");
    const static Poco::RegularExpression HAS_5DIGIT_TEMP_SUFFIX(".*t=[0-9]{5}$");

    if ( !std::getline(fis,line) ) {
      throw Poco::Exception(errorFormatter("Failed reading first line"));
    } else if ( !HAS_YES_SUFFIX.match(line) ) {
      throw Poco::Exception(errorFormatter("Expected \".* YES\" at end of first line"));
    } else if ( !std::getline(fis,line) ) {
      throw Poco::Exception(errorFormatter("Failed reading second line"));
    } else if ( !HAS_5DIGIT_TEMP_SUFFIX.match(line) ) {
      throw Poco::Exception(errorFormatter("Expected \".* t=[0-9]{5}$\" on second line"));
    } else {
      int iTemperatureData = Poco::NumberParser::parse(line.substr(line.size()-5));
      fTempCelcius = iTemperatureData / 1000.0;
    }
  } catch ( const Poco::Exception& ex ) {
    cerr << __PRETTY_FUNCTION__ << " " << ex.displayText() << endl;
    fTempCelcius = NAN;
  } catch ( ... ) {
    cerr << __PRETTY_FUNCTION__ << errorFormatter("Unhandled exception") << endl;
    fTempCelcius = NAN;
  }

  return fTempCelcius;
}

void OneWireThermSensor::createSensors(Poco::Util::LayeredConfiguration& conf, vector<unique_ptr<OneWireThermSensor>>& sensors) {
      
  std::string filter = conf.getString("oneWire[@filter]", "28-*");
  std::string path = conf.getString("oneWire[@path]", "/sys/bus/w1/devices");
  std::string search = path + "/" + filter + "/w1_slave";
  unsigned long maxCacheAgeMs = conf.getDouble("oneWire.maxCacheAgeMs",60000);
  std::set<string> fileNames;
  Poco::Glob::glob(search.c_str(),fileNames);
  
  for( string fileName : fileNames ) {
      string name = Poco::Path(fileName).parent().makeFile().getFileName();
      string xpath = string("oneWire.titles.title[@id='") + name + "'][@value]";
      string title = conf.getString( xpath, name);
      cout << __PRETTY_FUNCTION__ << " name=" << name << ", fileName=" << fileName << ", title=" << title << ", xpath=" << xpath << endl;
      sensors.push_back( make_unique<OneWireThermSensor>(name,fileName,title, maxCacheAgeMs));
  }
}

}