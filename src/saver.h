#ifndef IOQ3_MAP_EXPORTER_SRC_SAVER_H_
#define IOQ3_MAP_EXPORTER_SRC_SAVER_H_

#include <filesystem>

#include "scene.h"

namespace ioq3_map {

// Saves the Scene to a glTF file.
// Serializes geometry with both texture_uvs (TEXCOORD_0) and lightmap_uvs
// (TEXCOORD_1).
bool SaveScene(const Scene& scene, const std::filesystem::path& path);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_SRC_SAVER_H_
