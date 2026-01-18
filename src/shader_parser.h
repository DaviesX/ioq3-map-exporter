#ifndef IOQ3_MAP_EXPORTER_SHADER_PARSER_H_
#define IOQ3_MAP_EXPORTER_SHADER_PARSER_H_

#include <Eigen/Core>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "archives.h"

namespace ioq3_map {

using Q3ShaderName = std::string;

struct Q3Shader {
  Q3ShaderName name;
  int surface_flags = 0;
  int content_flags = 0;

  // Sunlight (from q3map_sun)
  Eigen::Vector3f q3map_sun_color = Eigen::Vector3f::Zero();
  float q3map_sun_intensity = 0.0f;
  // stored as degrees, elevation
  Eigen::Vector2f q3map_sun_direction = Eigen::Vector2f::Zero();

  // Emissive (from q3map_surfacelight and q3map_lightimage)
  float q3map_surfacelight = 0.0f;
  std::string q3map_lightimage;

  // Texture layers
  std::vector<std::string> texture_layers;
};

// Lists all *.shader files within the /scripts folder in the VFS.
std::vector<std::filesystem::path> ListQ3Shader(const VirtualFilesystem& vfs);

// Parses the content of shader scripts.
std::unordered_map<Q3ShaderName, Q3Shader> ParseShaderScripts(
    const VirtualFilesystem& vfs,
    const std::vector<std::filesystem::path>& shader_script_paths);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_SHADER_PARSER_H_
