#ifndef AUTOMATION_H
#define AUTOMATION_H


template <typename T> string asString(const T& t) { 
   ostringstream os; 
   os<<t; 
   return os.str(); 
}


#endif