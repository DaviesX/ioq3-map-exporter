#include "shader_parser.h"

#include <glog/logging.h>

#include <Eigen/Dense>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

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
namespace {

const char* kScriptFolder = "scripts";
const char* kShaderExtension = ".shader";
const std::vector<std::string> kTextureExtensions = {".tga", ".jpg", ".jpeg",
                                                     ".png"};

// Tokenizer helper
class Tokenizer {
 public:
  explicit Tokenizer(std::string_view content) : content_(content), pos_(0) {}

  std::string Next() {
    SkipWhitespaceAndComments();
    if (pos_ >= content_.size()) return "";

    if (content_[pos_] == '"') {
      return ParseQuoted();
    }

    if (content_[pos_] == '{' || content_[pos_] == '}') {
      return std::string(1, content_[pos_++]);
    }

    size_t start = pos_;
    while (pos_ < content_.size() && !std::isspace(content_[pos_]) &&
           content_[pos_] != '{' && content_[pos_] != '}') {
      pos_++;
    }
    return std::string(content_.substr(start, pos_ - start));
  }

  bool HasMore() const {
    size_t p = pos_;
    while (p < content_.size()) {
      if (std::isspace(content_[p])) {
        p++;
        continue;
      }
      if (p + 1 < content_.size() && content_[p] == '/' &&
          content_[p + 1] == '/') {
        p += 2;
        while (p < content_.size() && content_[p] != '\n') p++;
        continue;
      }
      return true;
    }
    return false;
  }

 private:
  void SkipWhitespaceAndComments() {
    while (pos_ < content_.size()) {
      if (std::isspace(content_[pos_])) {
        pos_++;
        continue;
      }
      if (pos_ + 1 < content_.size() && content_[pos_] == '/' &&
          content_[pos_] == '/') {
        pos_ += 2;
        while (pos_ < content_.size() && content_[pos_] != '\n') {
          pos_++;
        }
        continue;
      }
      break;
    }
  }

  std::string ParseQuoted() {
    pos_++;  // Skip opening quote
    size_t start = pos_;
    while (pos_ < content_.size() && content_[pos_] != '"') {
      pos_++;
    }
    std::string token = std::string(content_.substr(start, pos_ - start));
    if (pos_ < content_.size()) pos_++;  // Skip closing quote
    return token;
  }

  std::string_view content_;
  size_t pos_;
};

int GetSurfaceParmFlag(const std::string& parm) {
  std::string p = parm;
  std::transform(p.begin(), p.end(), p.begin(), ::tolower);

  if (p == "nodamage") return SURF_NODAMAGE;
  if (p == "slick") return SURF_SLICK;
  if (p == "sky") return SURF_SKY;
  if (p == "ladder") return SURF_LADDER;
  if (p == "noimpact") return SURF_NOIMPACT;
  if (p == "nomarks") return SURF_NOMARKS;
  if (p == "flesh") return SURF_FLESH;
  if (p == "nodraw") return SURF_NODRAW;
  if (p == "hint") return SURF_HINT;
  if (p == "skip") return SURF_SKIP;
  if (p == "nolightmap") return SURF_NOLIGHTMAP;
  if (p == "pointlight") return SURF_POINTLIGHT;
  if (p == "metalsteps") return SURF_METALSTEPS;
  if (p == "nosteps") return SURF_NOSTEPS;
  if (p == "nonsolid") return SURF_NONSOLID;
  if (p == "lightfilter") return SURF_LIGHTFILTER;
  if (p == "alphashadow") return SURF_ALPHASHADOW;
  if (p == "nodlight") return SURF_NODLIGHT;
  if (p == "dust") return SURF_DUST;
  if (p == "trans") return 0x0;
  return 0;
}

Q3WaveType GetWaveType(const std::string& wave_func) {
  std::string w = wave_func;
  std::transform(w.begin(), w.end(), w.begin(), ::tolower);

  if (w == "sin") return Q3WaveType::SINE;
  if (w == "triangle") return Q3WaveType::TRIANGLE;
  if (w == "square") return Q3WaveType::SQUARE;
  if (w == "sawtooth") return Q3WaveType::SAWTOOTH;
  if (w == "inversesawtooth") return Q3WaveType::INVERSE_SAWTOOTH;
  return Q3WaveType::NONE;
}

std::optional<BlendFunc> ParseBlendFunc(const std::string& func_name) {
  std::string f = func_name;
  std::transform(f.begin(), f.end(), f.begin(), ::tolower);

  if (f == "gl_zero") return BlendFunc::ZERO;
  if (f == "gl_one") return BlendFunc::ONE;
  if (f == "gl_dst_color") return BlendFunc::DST_COLOR;
  if (f == "gl_one_minus_dst_color") return BlendFunc::ONE_MINUS_DST_COLOR;
  if (f == "gl_src_alpha") return BlendFunc::SRC_ALPHA;
  if (f == "gl_one_minus_src_alpha") return BlendFunc::ONE_MINUS_SRC_ALPHA;
  if (f == "gl_dst_alpha") return BlendFunc::DST_ALPHA;
  if (f == "gl_one_minus_dst_alpha") return BlendFunc::ONE_MINUS_DST_ALPHA;
  if (f == "gl_src_color") return BlendFunc::SRC_COLOR;
  if (f == "gl_one_minus_src_color") return BlendFunc::ONE_MINUS_SRC_COLOR;

  // Defaults to One/Zero if unknown, or maybe we should log?
  // Let's assume ONE for now if invalid, but usually parser should handle this.
  return std::nullopt;
}

void ParseShaderParameter(const VirtualFilesystem& vfs,
                          const std::string& keyword, Tokenizer* tokenizer,
                          Q3Shader* shader) {
  std::string lower_keyword = keyword;
  std::transform(lower_keyword.begin(), lower_keyword.end(),
                 lower_keyword.begin(), ::tolower);

  if (lower_keyword == "surfaceparm") {
    std::string param = tokenizer->Next();
    shader->surface_flags |= GetSurfaceParmFlag(param);
  } else if (lower_keyword == "q3map_sun") {
    float r = std::stof(tokenizer->Next());
    float g = std::stof(tokenizer->Next());
    float b = std::stof(tokenizer->Next());
    shader->q3map_sun_color = Eigen::Vector3f(r, g, b);

    shader->q3map_sun_intensity = std::stof(tokenizer->Next());
    float degrees = std::stof(tokenizer->Next());
    float elevation = std::stof(tokenizer->Next());
    shader->q3map_sun_direction = Eigen::Vector2f(degrees, elevation);
  } else if (lower_keyword == "q3map_surfacelight") {
    shader->q3map_surfacelight = std::stof(tokenizer->Next());
  } else if (lower_keyword == "q3map_lightimage") {
    std::string path_str = tokenizer->Next();
    shader->q3map_lightimage = vfs.mount_point / path_str;
  } else if (lower_keyword == "q3map_sunlight") {
    // ignore
  } else if (lower_keyword == "q3map_sunmangle") {
    // ignore
    tokenizer->Next();
    tokenizer->Next();
    tokenizer->Next();
  }
}

std::optional<Q3TextureLayer> ParseShaderStages(const VirtualFilesystem& vfs,
                                                Tokenizer* tokenizer) {
  Q3TextureLayer result;

  // Inner block (stage/pass)
  while (tokenizer->HasMore()) {
    std::string inner = tokenizer->Next();
    if (inner == "}") break;
    if (inner == "{") {
      int depth = 1;
      while (depth > 0 && tokenizer->HasMore()) {
        std::string d = tokenizer->Next();
        if (d == "{")
          depth++;
        else if (d == "}")
          depth--;
      }
    }

    std::string keyword = inner;
    std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
    if (keyword == "map") {
      std::string texture_path = tokenizer->Next();
      if (texture_path == "$lightmap" || texture_path == "$whiteimage") {
        // We won't need to export lightmap or whiteimage.
        continue;
      }
      result.path = vfs.mount_point / texture_path;
    } else if (keyword == "tcmod") {
      std::string tcmod_op = tokenizer->Next();
      std::transform(tcmod_op.begin(), tcmod_op.end(), tcmod_op.begin(),
                     ::tolower);
      if (tcmod_op == "scale") {
        float s = std::stof(tokenizer->Next());
        float t = std::stof(tokenizer->Next());
        result.tcmod = Q3TCModScale{s, t};
      } else if (tcmod_op == "scroll") {
        float s = std::stof(tokenizer->Next());
        float t = std::stof(tokenizer->Next());
        result.tcmod = Q3TCModScroll{s, t};
      } else if (tcmod_op == "rotate") {
        float angle = std::stof(tokenizer->Next());
        result.tcmod = Q3TCModRotate{angle};
      } else if (tcmod_op == "turb") {
        std::string base_or_func = tokenizer->Next();
        Q3WaveType wave_type = GetWaveType(base_or_func);

        float base;
        if (wave_type == Q3WaveType::NONE) {
          base = std::stof(base_or_func);
        } else {
          base = std::stof(tokenizer->Next());
        }
        float amplitude = std::stof(tokenizer->Next());
        float phase = std::stof(tokenizer->Next());
        float frequency = std::stof(tokenizer->Next());
        result.tcmod =
            Q3TCModTurb{wave_type, base, amplitude, phase, frequency};
      } else if (tcmod_op == "stretch") {
        Q3WaveType wave_type = GetWaveType(tokenizer->Next());
        float base = std::stof(tokenizer->Next());
        float amplitude = std::stof(tokenizer->Next());
        float phase = std::stof(tokenizer->Next());
        float frequency = std::stof(tokenizer->Next());
        result.tcmod =
            Q3TCModStretch{wave_type, base, amplitude, phase, frequency};
      } else if (tcmod_op == "transform") {
        float m00 = std::stof(tokenizer->Next());
        float m01 = std::stof(tokenizer->Next());
        float m10 = std::stof(tokenizer->Next());
        float m11 = std::stof(tokenizer->Next());
        float t0 = std::stof(tokenizer->Next());
        float t1 = std::stof(tokenizer->Next());

        Q3TCModTransform transform;
        // clang-format off
        transform << m00, m01, t0,
                     m10, m11, t1;
        // clang-format on
        result.tcmod = transform;
      } else {
        LOG(WARNING) << "Unknown tcmod operation: " << tcmod_op;
      }
    } else if (keyword == "blendfunc") {
      std::string arg1 = tokenizer->Next();
      std::string lower_arg1 = arg1;
      std::transform(lower_arg1.begin(), lower_arg1.end(), lower_arg1.begin(),
                     ::tolower);

      if (lower_arg1 == "add") {
        result.blend_src = BlendFunc::ONE;
        result.blend_dst = BlendFunc::ONE;
      } else if (lower_arg1 == "filter") {
        result.blend_src = BlendFunc::DST_COLOR;
        result.blend_dst = BlendFunc::ZERO;
      } else if (lower_arg1 == "blend") {
        result.blend_src = BlendFunc::SRC_ALPHA;
        result.blend_dst = BlendFunc::ONE_MINUS_SRC_ALPHA;
      } else {
        // Explicit blendfunc <src> <dst>
        auto op1 = ParseBlendFunc(arg1);
        if (!op1) {
          DLOG(ERROR) << "Invalid blendfunc source: " << arg1;
          continue;
        }
        result.blend_src = op1.value();

        auto arg2 = tokenizer->Next();
        auto op2 = ParseBlendFunc(arg2);
        if (!op2) {
          DLOG(ERROR) << "Invalid blendfunc destination: " << arg2;
          continue;
        }
        result.blend_dst = op2.value();
      }
    }
  }

  if (result.path.empty()) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::filesystem::path> FindTexturePath(
    const std::filesystem::path& path) {
  std::filesystem::path candidate = path;
  if (std::filesystem::exists(candidate)) {
    return candidate;
  }

  // Try to find a valid extension
  for (const auto& ext : kTextureExtensions) {
    candidate.replace_extension(ext);
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  return std::nullopt;
}

void PruneInvalidTextureLayers(Q3Shader* shader) {
  for (auto it = shader->texture_layers.begin();
       it != shader->texture_layers.end();) {
    if (it->path.empty()) {
      LOG(WARNING) << "Shader " << shader->name << " has empty texture path.";
      it = shader->texture_layers.erase(it);
      continue;
    }

    auto found_path = FindTexturePath(it->path);
    if (!found_path) {
      // DLOG(WARNING) << "Shader " << shader->name << " has missing texture "
      //               << it->path;
      it = shader->texture_layers.erase(it);
      continue;
    }
    it->path = *found_path;
    ++it;
  }

  if (shader->q3map_lightimage) {
    auto found = FindTexturePath(*shader->q3map_lightimage);
    if (!found) {
      // DLOG(WARNING) << "Shader " << shader->name
      //               << " has missing q3map_lightimage "
      //               << *shader->q3map_lightimage;
      shader->q3map_lightimage = std::nullopt;
    } else {
      shader->q3map_lightimage = *found;
    }
  }
}

}  // namespace

std::vector<std::filesystem::path> ListQ3ShaderScripts(
    const VirtualFilesystem& vfs) {
  std::filesystem::recursive_directory_iterator dir_it;
  try {
    dir_it = std::filesystem::recursive_directory_iterator(vfs.mount_point /
                                                           kScriptFolder);
  } catch (const std::filesystem::filesystem_error& e) {
    LOG(ERROR) << "Failed to list shader scripts: " << e.what();
    return {};
  }

  std::vector<std::filesystem::path> result;
  for (const auto& entry : dir_it) {
    if (entry.is_regular_file() &&
        entry.path().extension() == kShaderExtension) {
      result.push_back(entry.path());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::unordered_map<Q3ShaderName, Q3Shader> ParseShaderScript(
    const VirtualFilesystem& vfs,
    const std::filesystem::path& shader_script_path) {
  std::ifstream file(shader_script_path);
  if (!file.is_open()) {
    LOG(ERROR) << "Failed to open shader file: " << shader_script_path;
    return {};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  std::unordered_map<Q3ShaderName, Q3Shader> result;

  Tokenizer tokenizer(content);
  while (tokenizer.HasMore()) {
    // The first line should be the shader name.
    std::string shader_name = tokenizer.Next();
    if (shader_name.empty() || shader_name == "}") {
      continue;  // Skip stray tokens or end of file
    }

    Q3Shader shader;
    shader.name = shader_name;

    // The next line should be an open brace containing shader parameters and
    // inner stages.
    std::string open_brace = tokenizer.Next();
    if (open_brace != "{") {
      LOG(WARNING) << "Expected '{' after shader name " << shader.name;
      continue;
    }

    while (tokenizer.HasMore()) {
      std::string token = tokenizer.Next();
      if (token == "}") {
        // End of shader.
        break;
      }

      if (token == "{") {
        // Inner block (stage/pass).
        auto texture_layer = ParseShaderStages(vfs, &tokenizer);
        if (texture_layer) {
          shader.texture_layers.push_back(*texture_layer);
        }
      } else {
        // Shader parameter.
        ParseShaderParameter(vfs, token, &tokenizer, &shader);
      }
    }

    PruneInvalidTextureLayers(&shader);
    result.insert_or_assign(shader.name, std::move(shader));
  }

  return result;
}

std::unordered_map<Q3ShaderName, Q3Shader> ParseShaderScripts(
    const VirtualFilesystem& vfs,
    const std::vector<std::filesystem::path>& shader_script_paths) {
  std::unordered_map<Q3ShaderName, Q3Shader> result;

  for (const auto& path : shader_script_paths) {
    auto parsed_shaders = ParseShaderScript(vfs, path);

    for (auto& [name, shader] : parsed_shaders) {
      result.emplace(name, std::move(shader));
    }
  }

  return result;
}

// A default shader contains only the one albedo texture layer. The shader name
// is the extensionless path to the texture in the VFS. If the texture is not
// found, return std::nullopt.
std::optional<Q3Shader> CreateDefaultShader(const Q3ShaderName& name,
                                            const VirtualFilesystem& vfs) {
  auto texture_path = FindTexturePath(vfs.mount_point / name);
  if (!texture_path) {
    LOG(WARNING) << "Could not find texture for shader " << name;
    return std::nullopt;
  }

  Q3Shader shader;
  shader.name = name;
  shader.texture_layers.push_back(Q3TextureLayer{.path = *texture_path});
  return shader;
}

}  // namespace ioq3_map
