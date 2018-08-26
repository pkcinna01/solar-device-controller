#ifndef AUTOMATION_SENSOR_H
#define AUTOMATION_SENSOR_H

#include <Poco/Thread.h>
#include <string>
#include <functional>
#include <iostream>

namespace automation {

  class Sensor {
    public:
    
    std::string name;

    const std::function<float()> getValueImp;

    Sensor(const std::string& name, const std::function<float()>& getValueImp)
    : name(name), getValueImp(getValueImp)
    {
    }
    
    virtual void setup()
    {   
    }
    
    virtual float getValue() const
    { 
      return getValueImp();
    }

    friend std::ostream &operator<<(std::ostream &os, const Sensor &s) {
      os << "Sensor{ name: " << s.name << ", value: " << s.getValue() << " }";
      return os;
    }
    
    template<typename ObjectPtr,typename MethodPtr> 
    static float sample(ObjectPtr obj, MethodPtr method, unsigned int cnt = 5, unsigned int intervalMs = 50) 
    {
      float sum = 0;
      for (int i = 0; i < cnt; i++)
      {
        sum += (obj->*method)();
        if ( cnt > 1 ) 
        {
          Poco::Thread::sleep(intervalMs);
          //delay(intervalMs);
        }
      }
      return sum/cnt;
    }
  };
}

#endif