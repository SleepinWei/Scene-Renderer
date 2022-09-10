#pragma once
#include<random>
const int PI = 3.1415926;
inline double random_double() {
	return rand() / (RAND_MAX + 1.0);
}
inline double random_double(double min, double max) {
	return min + (max - min) * random_double();
}
inline double clamp(double x, double min, double max) {
	return (x < min ? min : (x > max ? max : x));
}

