#pragma once
#include<iostream>
using std::cout;

#define OUTPUT(prompt,x) cout << prompt << " " << x << " at " << __FILE__ << " line " << __LINE__ << '\n';
#define INFO(x) OUTPUT("[INFO]", x) 
#define DEBUG(x) OUTPUT("[DEBUG]", x)
#define WARNING(x) OUTPUT("[WARNING]",x)
#define ERROR(x) OUTPUT("[ERROR]",x)