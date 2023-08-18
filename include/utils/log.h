#pragma once
#include<iostream>
using std::cout;

#define LINE_NUMBER " at " << __FILE__ << " line " << __LINE__
#define OUTPUT(prefix,x,postfix) cout << prefix << " " << x << postfix << '\n';
#define LOG_INFO(x) OUTPUT("[INFO]", x, "") 
#define LOG_DEBUG(x) OUTPUT("[DEBUG]", x,LINE_NUMBER)
#define LOG_WARNING(x) OUTPUT("[WARNING]",x,LINE_NUMBER)
#define LOG_ERROR(x) OUTPUT("[ERROR]",x,LINE_NUMBER)