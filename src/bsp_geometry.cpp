#include "bsp_geometry.h"

#include <glog/logging.h>

#include <cstring>
#include <string_view>

#include "bsp.h"

namespace ioq3_map {
namespace {

// Helper to get raw pointer from string_view
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

}  // namespace

std::unordered_map<BSPSurfaceIndex, BSPGeometry> BuildBSPGeometries(
    const BSP& bsp) {
  std::unordered_map<BSPSurfaceIndex, BSPGeometry> geometries;

  size_t num_faces = 0;
  const dsurface_t* faces =
      GetLumpData<dsurface_t>(bsp, LumpType::Faces, &num_faces);

  size_t num_verts = 0;
  const vertex_t* vertices =
      GetLumpData<vertex_t>(bsp, LumpType::Vertexes, &num_verts);

  size_t num_meshverts = 0;
  const int* meshverts =
      GetLumpData<int>(bsp, LumpType::MeshVerts, &num_meshverts);

  if (!faces || !vertices) {
    LOG(ERROR) << "Missing faces or vertices";
    return geometries;
  }

  for (size_t i = 0; i < num_faces; ++i) {
    const dsurface_t& face = faces[i];
    BSPGeometry geo;
    geo.texture_index = face.shader_no;

    // Validate vertex range
    if (face.first_vert < 0 ||
        face.first_vert + face.num_verts > static_cast<int>(num_verts)) {
      LOG(ERROR) << "Invalid vertex range for face " << i;
      continue;
    }

    // Common: Extract vertices for this face
    std::vector<vertex_t> face_vertices;
    face_vertices.reserve(face.num_verts);
    for (int v = 0; v < face.num_verts; ++v) {
      face_vertices.push_back(vertices[face.first_vert + v]);
    }

    switch (face.surface_type) {
      case MapSurfaceType::PLANAR:
      case MapSurfaceType::TRIANGLE_SOUP: {
        if (!meshverts) {
          LOG(ERROR) << "Missing meshverts for face " << i;
          continue;
        }

        // Validate index range
        if (face.first_index < 0 || face.first_index + face.num_indexes >
                                        static_cast<int>(num_meshverts)) {
          LOG(ERROR) << "Invalid index range for face " << i;
          continue;
        }

        std::vector<int> face_indices;
        face_indices.reserve(face.num_indexes);
        for (int idx = 0; idx < face.num_indexes; ++idx) {
          // Meshverts are offsets relative to firstVert
          int meshvert = meshverts[face.first_index + idx];
          face_indices.push_back(meshvert);
        }

        if (face.surface_type == MapSurfaceType::PLANAR) {
          geo.primitive =
              BSPPolygon{std::move(face_vertices), std::move(face_indices)};
        } else {
          geo.primitive =
              BSPMesh{std::move(face_vertices), std::move(face_indices)};
        }

        geometries.emplace(static_cast<BSPSurfaceIndex>(i), std::move(geo));
        break;
      }
      case MapSurfaceType::PATCH: {
        // For patches, face.num_verts should be width * height
        // Vertices are control points
        BSPPatch patch;
        patch.width = face.patch_width;
        patch.height = face.patch_height;
        patch.control_points = std::move(face_vertices);

        geo.primitive = std::move(patch);
        geometries.emplace(static_cast<BSPSurfaceIndex>(i), std::move(geo));
        break;
      }
      default: {
        // Ignore other types (FLARE, BAD)
        break;
      }
    }
  }

  return geometries;
}

}  // namespace ioq3_map
