
#ifndef _AUTOMATION_ATTRIBUTE_CONTAINER_H_
#define _AUTOMATION_ATTRIBUTE_CONTAINER_H_

#include "json/Printable.h"

#include <string>

namespace automation {

  class AttributeContainer : public json::Printable {

    public:

    virtual bool setAttribute(const char* pszKey, const char* pszVal, ostream* pResponseStream = nullptr){
      return true;
    }
    
    virtual std::string getTitle() const = 0;
  };


  class NamedContainer : public AttributeContainer {

    public:

    mutable std::string name;
    
    NamedContainer(const std::string& name) : name(name) {
    }

    bool setAttribute(const char* pszKey, const char* pszVal, ostream* pResponseStream = nullptr) override {

      if ( pszKey && !strcasecmp("name",pszKey) ) {
        name = pszVal;
      } else {
        return false;
      }
      if ( pResponseStream ) {
        (*pResponseStream) << "'" << getTitle() << "' " << pszKey << "=" << pszVal;
      }
      return true; // attribute found and set
    }

    virtual std::string getTitle() const override {
      return name;
    }
  };

  template<typename T>
  class NamedItemVector : public std::vector<T> {
  public:

    NamedItemVector() : std::vector<T>(){}
    NamedItemVector( std::vector<T>& v ) : std::vector<T>(v) {}
    std::vector<T>& filterByNames( const char* pszCommaDelimitedNames, std::vector<T>& resultVec, bool bInclude = true) {
      if ( pszCommaDelimitedNames == nullptr || strlen(pszCommaDelimitedNames) == 0 ) {
        pszCommaDelimitedNames = "*";
      }
      istringstream nameStream(pszCommaDelimitedNames);
      string str;
      vector<string> nameVec;
      while (std::getline(nameStream, str, ',')) {
        nameVec.push_back(str);
        if( nameStream.eof( ) ) {
          break;
        }
      }
      for( const T& item : *this ) {
        for( const string& namePattern : nameVec) {
          if (bInclude==text::WildcardMatcher::test(namePattern.c_str(),item->name.c_str()) ) {
            resultVec.push_back(item);
            break;
          }
        }
      }
      return resultVec;
    };
  };

  class NamedContainerVector : public NamedItemVector<NamedContainer*> {
  public:
    NamedContainerVector() : NamedItemVector<NamedContainer*>(){}
    NamedContainerVector( std::vector<NamedContainer*>& v ) : NamedItemVector<NamedContainer*>(v) {}
  };

}

#endif
