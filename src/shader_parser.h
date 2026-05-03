#ifndef IOQ3_MAP_EXPORTER_SHADER_PARSER_H_
#define IOQ3_MAP_EXPORTER_SHADER_PARSER_H_

#include <Eigen/Core>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "archives.h"

// Basic surface flags from surfaceflags.h
#define SURF_NODAMAGE 0x1
#define SURF_SLICK 0x2
#define SURF_SKY 0x4
#define SURF_LADDER 0x8
#define SURF_NOIMPACT 0x10
#define SURF_NOMARKS 0x20
#define SURF_FLESH 0x40
#define SURF_NODRAW 0x80
#define SURF_HINT 0x100
#define SURF_SKIP 0x200
#define SURF_NOLIGHTMAP 0x400
#define SURF_POINTLIGHT 0x800
#define SURF_METALSTEPS 0x1000
#define SURF_NOSTEPS 0x2000
#define SURF_NONSOLID 0x4000
#define SURF_LIGHTFILTER 0x8000
#define SURF_ALPHASHADOW 0x10000
#define SURF_NODLIGHT 0x20000
#define SURF_DUST 0x40000

namespace ioq3_map {

enum class Q3WaveType {
  NONE,
  SINE,
  TRIANGLE,
  SQUARE,
  SAWTOOTH,
  INVERSE_SAWTOOTH,
};

using Q3ShaderName = std::string;

struct Q3TCModNoOp {};

struct Q3TCModScale {
  float s_scale;
  float t_scale;
};

struct Q3TCModScroll {
  float s_rate;
  float t_rate;
};

struct Q3TCModRotate {
  float angle;
};

struct Q3TCModTurb {
  Q3WaveType wave_type;  // Optional.
  float base;
  float amplitude;
  float phase;
  float frequency;
};

struct Q3TCModStretch {
  Q3WaveType wave_type;  // Required.
  float base;
  float amplitude;
  float phase;
  float frequency;
};

using Q3TCModTransform = Eigen::Matrix<float, 2, 3>;  // Affine transform.

enum class BlendFunc {
  ZERO,
  ONE,
  DST_COLOR,
  ONE_MINUS_DST_COLOR,
  SRC_ALPHA,
  ONE_MINUS_SRC_ALPHA,
  DST_ALPHA,
  ONE_MINUS_DST_ALPHA,
  SRC_COLOR,
  ONE_MINUS_SRC_COLOR,
};

enum class RgbGenType {
  IDENTITY,
  VERTEX,
  WAVE,
  IDENTITY_LIGHTING,
  EXACT_VERTEX
};

struct Q3RgbGen {
  RgbGenType type = RgbGenType::IDENTITY;
  Q3WaveType wave_type = Q3WaveType::NONE;
  float base = 0.0f;
  float amplitude = 0.0f;
  float phase = 0.0f;
  float frequency = 0.0f;

  bool operator==(const Q3RgbGen& other) const {
      return type == other.type && wave_type == other.wave_type && 
             base == other.base && amplitude == other.amplitude && 
             phase == other.phase && frequency == other.frequency;
  }
};

struct Q3TextureLayer {
  std::filesystem::path path;
  std::variant<Q3TCModNoOp, Q3TCModScale, Q3TCModScroll, Q3TCModRotate,
               Q3TCModTurb, Q3TCModStretch, Q3TCModTransform>
      tcmod = Q3TCModNoOp{};

  // Blending
  BlendFunc blend_src = BlendFunc::ONE;
  BlendFunc blend_dst = BlendFunc::ZERO;

  // Color Generation
  Q3RgbGen rgbgen;

  bool operator==(const Q3TextureLayer& other) const {
    return path == other.path && blend_src == other.blend_src &&
           blend_dst == other.blend_dst && rgbgen == other.rgbgen;
  }
};

enum class Q3CullType {
  FRONT,
  BACK,
  NONE
};

struct Q3Shader {
  // Original name specified in the shader script.
  Q3ShaderName name;

  // Culling
  Q3CullType cull = Q3CullType::FRONT;

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

  // If present, this texture is used for light color/emission.
  std::optional<std::filesystem::path> q3map_lightimage;

  // Texture layers
  std::vector<Q3TextureLayer> texture_layers;
};

// Lists all *.shader files within the /scripts folder in the VFS. It returns
// the OS paths for every shader file found.
std::vector<std::filesystem::path> ListQ3ShaderScripts(
    const VirtualFilesystem& vfs);

// Parses the content of a shader script from a full OS path.
std::unordered_map<Q3ShaderName, Q3Shader> ParseShaderScript(
    const VirtualFilesystem& vfs,
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
