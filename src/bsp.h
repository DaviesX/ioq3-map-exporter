#ifndef IOQ3_MAP_BSP_H_
#define IOQ3_MAP_BSP_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

bool IsValidBsp(const std::filesystem::path& bsp_file_path);
std::optional<BSP> LoadBsp(const std::filesystem::path& bsp_file_path);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_BSP_H_
