#ifndef IOQ3_MAP_BSP_H_
#define IOQ3_MAP_BSP_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "glog/logging.h"

namespace ioq3_map {

enum class LumpType {
  Entities = 0,
  Textures = 1,
  Planes = 2,
  Nodes = 3,
  Leafs = 4,
  LeafFaces = 5,
  LeafBrushes = 6,
  Models = 7,
  Brushes = 8,
  BrushSides = 9,
  Vertexes = 10,
  MeshVerts = 11,
  Effects = 12,
  Faces = 13,
  Lightmaps = 14,
  Lightvol = 15,
  VisData = 16
};

struct BSP {
  std::string buffer;
  std::unordered_map<LumpType, std::string_view> lumps;
};

// Helper to get raw pointer from string_view.
template <typename T>
const T* GetLumpData(const BSP& bsp, LumpType type, size_t* count) {
  auto it = bsp.lumps.find(type);
  if (it == bsp.lumps.end() || it->second.empty()) {
    *count = 0;
    return nullptr;
  }
  const std::string_view& lump_data = it->second;
  if (lump_data.size() % sizeof(T) != 0) {
    LOG(ERROR) << "Invalid lump size for " << static_cast<int>(type);
    *count = 0;
    return nullptr;
  }
  *count = lump_data.size() / sizeof(T);
  return reinterpret_cast<const T*>(lump_data.data());
}

// Checks if the file is a valid BSP file.
bool IsValidBsp(const std::filesystem::path& bsp_file_path);

// Loads a BSP file into memory.
std::optional<BSP> LoadBsp(const std::filesystem::path& bsp_file_path);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_BSP_H_
