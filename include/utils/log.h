#pragma once
#include<iostream>
using std::cout;

#define OUTPUT(prompt,x) cout << prompt << " " << x << " at " << __FILE__ << " line " << __LINE__ << '\n';
#define LOG_INFO(x) OUTPUT("[INFO]", x) 
#define LOG_DEBUG(x) OUTPUT("[DEBUG]", x)
#define LOG_WARNING(x) OUTPUT("[WARNING]",x)
#define LOG_ERROR(x) OUTPUT("[ERROR]",x)