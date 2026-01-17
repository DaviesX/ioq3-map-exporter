#ifndef IOQ3_MAP_BSP_GEOMETRY_H_
#define IOQ3_MAP_BSP_GEOMETRY_H_

#include <unordered_map>
#include <variant>
#include <vector>

#include "bsp.h"

namespace ioq3_map {

// --- Raw Q3 Data Structures (matching qfiles.h) ---

using byte = unsigned char;
using vec3_t = float[3];
using vec2_t = float[2];

struct drawVert_t {
  vec3_t xyz;
  vec2_t st;
  vec2_t lightmap;
  vec3_t normal;
  byte color[4];
};

enum class MapSurfaceType : int {
  BAD = 0,
  PLANAR = 1,
  PATCH = 2,
  TRIANGLE_SOUP = 3,
  FLARE = 4
};

struct dsurface_t {
  int shaderNum;
  int fogNum;
  int surfaceType;

  int firstVert;
  int numVerts;

  int firstIndex;
  int numIndexes;

  int lightmapNum;
  int lightmapX, lightmapY;
  int lightmapWidth, lightmapHeight;

  vec3_t lightmapOrigin;
  vec3_t lightmapVecs[3];

  int patchWidth;
  int patchHeight;
};

// --- High-Level Geometry Abstractions ---

using BSPSurfaceIndex = int;
using BSPTextureIndex = int;

// For MST_TRIANGLE_SOUP (Type 3)
struct BSPMesh {
  std::vector<drawVert_t> vertices;
  std::vector<int> indices;
};

// For MST_PLANAR (Type 1)
struct BSPPolygon {
  std::vector<drawVert_t> vertices;
  std::vector<int> indices;
};

// For MST_PATCH (Type 2)
struct BSPPatch {
  int width;
  int height;
  std::vector<drawVert_t> control_points;
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
