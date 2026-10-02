// Single translation unit for header-only libraries that need an implementation.
// tinygltf's bundled json 3.5 has no value_t::binary, which its loader uses.
// Use the project's nlohmann json 3.11 instead.
#define TINYGLTF_NO_INCLUDE_JSON
#include <json/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include <tinygltf/tiny_gltf.h>
