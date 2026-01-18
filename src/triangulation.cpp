#include "triangulation.h"

namespace ioq3_map {

BSPMesh Triangulate(const BSPPolygon& polygon) {
  BSPMesh mesh;
  mesh.vertices = polygon.vertices;

  const size_t num_verts = polygon.vertices.size();
  if (num_verts < 3) {
    return mesh;
  }

  // Create a triangle fan: (0, 1, 2), (0, 2, 3), ...
  for (size_t i = 1; i < num_verts - 1; ++i) {
    mesh.indices.push_back(0);
    mesh.indices.push_back(static_cast<int>(i));
    mesh.indices.push_back(static_cast<int>(i + 1));
  }

  return mesh;
}

}  // namespace ioq3_map
