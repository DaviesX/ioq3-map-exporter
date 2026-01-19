#include "bsp_material.h"

#include <glog/logging.h>

#include <cstring>
#include <string>
#include <unordered_set>

namespace ioq3_map {
namespace {

const std::unordered_set<Q3ShaderName> kShouldSkipShaders = {
    "noshader",
};

}  // namespace

std::unordered_map<BSPTextureIndex, BSPMaterial> BuildBSPMaterials(
    const BSP& bsp,
    const std::unordered_map<Q3ShaderName, Q3Shader>& parsed_shaders,
    const CreateDefaultShaderFn& create_default_shader) {
  std::unordered_map<BSPTextureIndex, BSPMaterial> materials;

  size_t num_shaders = 0;
  const dshader_t* shader_lump =
      GetLumpData<dshader_t>(bsp, LumpType::Textures, &num_shaders);

  if (!shader_lump) {
    LOG(ERROR) << "No shader lump found in BSP.";
    return materials;
  }

  for (size_t i = 0; i < num_shaders; ++i) {
    const dshader_t& ds = shader_lump[i];

    // Ensure null termination safe read
    std::string texture_name(ds.shader, strnlen(ds.shader, kMaxQPath));
    if (kShouldSkipShaders.count(texture_name)) {
      // There are some system shaders that we are not interested in.
      continue;
    }

    // Q3 shader names are case-insensitive often, but the map/files are usually
    // consistent. However, the shader parser might have stored them exactly as
    // in the file. Let's try direct lookup first.

    BSPMaterial material;
    auto it = parsed_shaders.find(texture_name);
    if (it != parsed_shaders.end()) {
      // Found in shader scripts, copy properties
      material = it->second;
    } else if (create_default_shader) {
      // No shader found, create default.
      auto default_shader = create_default_shader(texture_name);
      if (!default_shader) {
        LOG(WARNING)
            << "Unable to create default shader for " << texture_name
            << " and no associated shader definition was found. Skipping.";
        continue;
      }
      material = *default_shader;
    } else {
      LOG(WARNING) << "No associated shader definition was found. Skipping.";
      continue;
    }

    // Always mix in flags from Lump 1 as they define the map-specific
    // overrides? Actually, Lump 1 flags are usually what the compiler baked. So
    // we should trust Lump 1 flags for surface/content.
    material.surface_flags = ds.surface_flags;
    material.content_flags = ds.content_flags;

    materials[static_cast<BSPTextureIndex>(i)] = material;
  }

  return materials;
}

}  // namespace ioq3_map
