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

struct Q3TextureLayer {
  std::filesystem::path path;

  // TODO: Add blending mode, etc.

  bool operator==(const Q3TextureLayer& other) const {
    return path == other.path;
  }
};

struct Q3Shader {
  // Original name specified in the shader script.
  Q3ShaderName name;

  // Q3 flags
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
  std::vector<Q3TextureLayer> texture_layers;
};

// Lists all *.shader files within the /scripts folder in the VFS. It returns
// the OS paths for every shader file found.
std::vector<std::filesystem::path> ListQ3ShaderScripts(
    const VirtualFilesystem& vfs);

// Parses the content of a shader script from a full OS path.
std::unordered_map<Q3ShaderName, Q3Shader> ParseShaderScript(
    const std::filesystem::path& shader_script_path);

// Parses the content of shader scripts.
std::unordered_map<Q3ShaderName, Q3Shader> ParseShaderScripts(
    const VirtualFilesystem& vfs,
    const std::vector<std::filesystem::path>& shader_script_paths);

// A default shader contains only the one albedo texture layer. The shader name
// is the extensionless path to the texture in the VFS. If the texture is not
// found, return std::nullopt.
std::optional<Q3Shader> CreateDefaultShader(const Q3ShaderName& name,
                                            const VirtualFilesystem& vfs);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_SHADER_PARSER_H_
