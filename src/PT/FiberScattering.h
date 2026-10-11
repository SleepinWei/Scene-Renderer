#pragma once
#include <glm/glm.hpp>
namespace pt { namespace fiber {
using glm::vec2;using glm::vec3;using glm::vec4;
using glm::clamp;using glm::max;using glm::abs;using glm::sqrt;using glm::atan;using glm::asin;
using glm::exp;using glm::log;using glm::pow;using glm::dot;using glm::cos;using glm::sin;using glm::floor;using glm::mix;using glm::length;
#define FIBER_INLINE inline
#include "../rhi/shaders/pt-fiber.glsl"
#undef FIBER_INLINE
}}
