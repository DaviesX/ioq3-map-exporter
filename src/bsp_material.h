#ifndef IOQ3_MAP_EXPORTER_BSP_MATERIAL_H_
#define IOQ3_MAP_EXPORTER_BSP_MATERIAL_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "bsp.h"
#include "shader_parser.h"

namespace ioq3_map {

// Q3 defined constants
inline constexpr int kMaxQPath = 64;

// Lump 1: Shaders
struct dshader_t {
  char shader[kMaxQPath];
  int surface_flags;
  int content_flags;
};

using BSPMaterial = Q3Shader;

// Index to Lump 1 (Textures)
using BSPTextureIndex = int;

// Builds a map of BSPTextureIndex to Material.
// Merges information from Lump 1 (names/flags) and parsed shader scripts
// (sun/emission).
std::unordered_map<BSPTextureIndex, BSPMaterial> BuildBSPMaterials(
    const BSP& bsp,
    const std::unordered_map<Q3ShaderName, Q3Shader>& parsed_shaders,
    const VirtualFilesystem& vfs);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_BSP_MATERIAL_H_
