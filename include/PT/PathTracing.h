#pragma once
#include <cstdlib>
#include <glm/glm.hpp>
#include <limits>
#include <vector>
// #include"PTVec.h"
#include "PT/PTRay.h"
#include "PT/PTScene.h"
#include "PT/hittable.h"

using glm::vec3;


// const vec3 background = vec3(0.5, 0.7, 1.0);
void PathTracingRun(shared_ptr<PTScene> pt_scene);
