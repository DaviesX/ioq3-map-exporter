#ifndef IOQ3_MAP_BSP_GEOMETRY_H_
#define IOQ3_MAP_BSP_GEOMETRY_H_

#include <stdint.h>

#include <Eigen/Dense>  // IWYU pragma: keep
#include <unordered_map>
#include <variant>
#include <vector>

namespace ioq3_map {

struct BSP;

// --- Raw Q3 Data Structures (matching qfiles.h) ---
// Vertex data layout as stored in the BSP file.
struct vertex_t {
  Eigen::Vector3f xyz;
  Eigen::Vector2f st;
  Eigen::Vector2f lightmap;
  Eigen::Vector3f normal;
  uint8_t color[4];
};

enum class MapSurfaceType : int {
  BAD = 0,
  PLANAR = 1,
  PATCH = 2,
  TRIANGLE_SOUP = 3,
  FLARE = 4
};

// Face data layout as stored in the BSP file.
struct dsurface_t {
  int shader_no;
  int fog_num;
  MapSurfaceType surface_type;

  int first_vert;
  int num_verts;

  int first_index;
  int num_indexes;

  int lightmap_num;
  int lightmap_x, lightmap_y;
  int lightmap_width, lightmap_height;

  Eigen::Vector3f lightmap_origin;
  Eigen::Vector3f lightmap_vecs[3];

  int patch_width;
  int patch_height;
};

// --- High-Level Geometry Abstractions ---

using BSPSurfaceIndex = int;
using BSPTextureIndex = int;

// For MST_TRIANGLE_SOUP (Type 3)
struct BSPMesh {
  std::vector<vertex_t> vertices;
  std::vector<int> indices;
};

// For MST_PLANAR (Type 1)
struct BSPPolygon {
  std::vector<vertex_t> vertices;
  std::vector<int> indices;
};

// For MST_PATCH (Type 2)
struct BSPPatch {
  int width;
  int height;
  std::vector<vertex_t> control_points;
};

struct BSPGeometry {
  std::variant<BSPMesh, BSPPolygon, BSPPatch> primitive;
  BSPTextureIndex texture_index;
};

// Parses the BSP lumps to build internal geometry representations.
std::unordered_map<BSPSurfaceIndex, BSPGeometry> BuildBSPGeometries(
    const BSP& bsp);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_BSP_GEOMETRY_H_
