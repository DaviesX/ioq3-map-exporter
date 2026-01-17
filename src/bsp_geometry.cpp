#include "bsp_geometry.h"

#include <cstring>

namespace ioq3_map {

namespace {

// Helper to get raw pointer from string_view
template <typename T>
const T* GetLumpData(const BSP& bsp, LumpType type, size_t& count) {
  auto it = bsp.lumps.find(type);
  if (it == bsp.lumps.end() || it->second.empty()) {
    count = 0;
    return nullptr;
  }
  count = it->second.size() / sizeof(T);
  return reinterpret_cast<const T*>(it->second.data());
}

}  // namespace

std::unordered_map<BSPSurfaceIndex, BSPGeometry> BuildBSPGeometries(
    const BSP& bsp) {
  std::unordered_map<BSPSurfaceIndex, BSPGeometry> geometries;

  size_t num_faces = 0;
  const dsurface_t* faces =
      GetLumpData<dsurface_t>(bsp, LumpType::Faces, num_faces);

  size_t num_verts = 0;
  const drawVert_t* vertices =
      GetLumpData<drawVert_t>(bsp, LumpType::Vertexes, num_verts);

  size_t num_meshverts = 0;
  const int* meshverts =
      GetLumpData<int>(bsp, LumpType::MeshVerts, num_meshverts);

  if (!faces || !vertices) {
    return geometries;
  }

  for (size_t i = 0; i < num_faces; ++i) {
    const dsurface_t& face = faces[i];
    BSPGeometry geo;
    geo.texture_index = face.shaderNum;

    // Validate vertex range
    if (face.firstVert < 0 ||
        face.firstVert + face.numVerts > static_cast<int>(num_verts)) {
      continue;
    }

    // Common: Extract vertices for this face
    std::vector<drawVert_t> face_vertices;
    face_vertices.reserve(face.numVerts);
    for (int v = 0; v < face.numVerts; ++v) {
      face_vertices.push_back(vertices[face.firstVert + v]);
    }

    if (face.surfaceType == static_cast<int>(MapSurfaceType::PLANAR) ||
        face.surfaceType == static_cast<int>(MapSurfaceType::TRIANGLE_SOUP)) {
      if (!meshverts) {
        continue;
      }

      // Validate index range
      if (face.firstIndex < 0 ||
          face.firstIndex + face.numIndexes > static_cast<int>(num_meshverts)) {
        continue;
      }

      std::vector<int> face_indices;
      face_indices.reserve(face.numIndexes);
      for (int idx = 0; idx < face.numIndexes; ++idx) {
        // Meshverts are offsets relative to firstVert
        int meshvert = meshverts[face.firstIndex + idx];
        face_indices.push_back(meshvert);
      }

      if (face.surfaceType == static_cast<int>(MapSurfaceType::PLANAR)) {
        geo.primitive =
            BSPPolygon{std::move(face_vertices), std::move(face_indices)};
      } else {
        geo.primitive =
            BSPMesh{std::move(face_vertices), std::move(face_indices)};
      }

      geometries.emplace(static_cast<BSPSurfaceIndex>(i), std::move(geo));

    } else if (face.surfaceType == static_cast<int>(MapSurfaceType::PATCH)) {
      // For patches, face.numVerts should be width * height
      // Vertices are control points
      BSPPatch patch;
      patch.width = face.patchWidth;
      patch.height = face.patchHeight;
      patch.control_points = std::move(face_vertices);

      geo.primitive = std::move(patch);
      geometries.emplace(static_cast<BSPSurfaceIndex>(i), std::move(geo));
    } else {
      // Ignore other types (FLARE, BAD)
    }
  }

  return geometries;
}

}  // namespace ioq3_map
