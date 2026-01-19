#include "shader_parser.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

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

void ParseShaderParameter(const std::string& keyword, Tokenizer* tokenizer,
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
    shader->q3map_lightimage = tokenizer->Next();
  } else if (lower_keyword == "q3map_sunlight") {
    // ignore
  } else if (lower_keyword == "q3map_sunmangle") {
    // ignore
    tokenizer->Next();
    tokenizer->Next();
    tokenizer->Next();
  }
}

std::vector<Q3TextureLayer> ParseShaderStages(Tokenizer* tokenizer) {
  std::vector<Q3TextureLayer> result;

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

    std::string lower_inner = inner;
    std::transform(lower_inner.begin(), lower_inner.end(), lower_inner.begin(),
                   ::tolower);
    if (lower_inner == "map") {
      std::string texture_path = tokenizer->Next();
      if (texture_path == "$lightmap" || texture_path == "$whiteimage") {
        // We won't need to export lightmap or whiteimage.
        continue;
      }
      result.push_back(Q3TextureLayer{.path = texture_path});
    }
  }

  return result;
}

bool VerifyShader(const Q3Shader& shader, const VirtualFilesystem& vfs) {
  for (const auto& layer : shader.texture_layers) {
    if (layer.path.empty()) {
      LOG(WARNING) << "Shader " << shader.name << " has empty texture path.";
      return false;
    }

    std::filesystem::path texture_path = vfs.mount_point / layer.path;
    if (!std::filesystem::exists(texture_path)) {
      LOG(WARNING) << "Shader " << shader.name << " has missing texture "
                   << texture_path;
      return false;
    }
  }
  return true;
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
        shader.texture_layers = ParseShaderStages(&tokenizer);
      } else {
        // Shader parameter.
        ParseShaderParameter(token, &tokenizer, &shader);
      }
    }

    if (!VerifyShader(shader, vfs)) {
      LOG(WARNING) << "Invalid shader " << shader.name;
      continue;
    }

    result.emplace(shader.name, std::move(shader));
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
  // Common texture extensions in Q3
  static const std::vector<std::string> extensions = {".tga", ".jpg", ".png",
                                                      ".jpeg"};

  for (const auto& ext : extensions) {
    std::string filename = name + ext;
    std::filesystem::path full_path = vfs.mount_point / filename;

    // We use std::filesystem::exists to check.
    // Ensure we handle case sensitivity if possible, but IOQ3 is mixed.
    // For now, rely on FS or assuming lower case matching (Q3 is usually case
    // insensitive). The VFS/Filesystem on Linux is case sensitive, which might
    // be a problem if the PK3 was authored on Windows.
    // Ideally we would search, but for Phase 1/2 we'll assume correct casing or
    // simple existence.
    if (!std::filesystem::exists(full_path)) {
      continue;
    }

    Q3Shader shader;
    shader.name = name;
    shader.texture_layers.push_back(Q3TextureLayer{.path = filename});
    return shader;
  }

  return std::nullopt;
}

}  // namespace ioq3_map
