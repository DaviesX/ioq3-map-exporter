#include "scene.h"

#include <unordered_map>

#include "bsp.h"
#include "bsp_geometry.h"
#include "triangulation.h"

namespace ioq3_map {

namespace {

// Q3 is Z-up. glTF is Y-up.
// Standard conversion: Rotate -90 degrees around X axis.
// x' = x
// y' = z
// z' = -y
Eigen::Vector3f TransformPoint(const Eigen::Vector3f& p) {
  // Scale (inches to meters)
  constexpr float kScale = 0.0254f;
  return Eigen::Vector3f(p.x() * kScale, p.z() * kScale, -p.y() * kScale);
}

Eigen::Vector3f TransformNormal(const Eigen::Vector3f& n) {
  // Rotate -90 degrees around X axis.
  return Eigen::Vector3f(n.x(), n.z(), -n.y());
}

Eigen::Vector2f TransformUV(const Eigen::Vector2f& uv) {
  return Eigen::Vector2f(uv.x(), uv.y());
}

struct dshader_t {
  char shader[64];
  int surfaceFlags;
  int contentFlags;
};

// Helper to get raw pointer from string_view (Duplicated from bsp_geometry.cpp
// for now)
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

void ToGeometry(const BSPMesh& mesh, Geometry* out_geometry) {
  out_geometry->vertices.reserve(mesh.vertices.size());
  out_geometry->normals.reserve(mesh.vertices.size());
  out_geometry->texture_uvs.reserve(mesh.vertices.size());
  out_geometry->lightmap_uvs.reserve(mesh.vertices.size());

  for (const auto& v : mesh.vertices) {
    out_geometry->vertices.push_back(TransformPoint(v.xyz));
    out_geometry->normals.push_back(TransformNormal(v.normal));
    out_geometry->texture_uvs.push_back(TransformUV(v.st));
    out_geometry->lightmap_uvs.push_back(TransformUV(v.lightmap));
  }

  out_geometry->indices.reserve(mesh.indices.size());
  for (int idx : mesh.indices) {
    out_geometry->indices.push_back(static_cast<uint32_t>(idx));
  }
}

}  // namespace

Scene AssembleBSPObjects(
    const BSP& bsp,
    const std::unordered_map<BSPSurfaceIndex, BSPGeometry>& bsp_geometries) {
  Scene scene;

  for (const auto& [surface_idx, geo] : bsp_geometries) {
    if (std::holds_alternative<BSPPatch>(geo.primitive)) {
      // TODO: Handle Patches (Tessellation)
      continue;
    }

    Geometry out_geo;
    out_geo.material_id = geo.texture_index;
    out_geo.transform = Eigen::Affine3f::Identity();

    if (std::holds_alternative<BSPPolygon>(geo.primitive)) {
      const auto& poly = std::get<BSPPolygon>(geo.primitive);
      BSPMesh mesh = Triangulate(poly);
      ToGeometry(mesh, &out_geo);
    } else if (std::holds_alternative<BSPMesh>(geo.primitive)) {
      const auto& mesh = std::get<BSPMesh>(geo.primitive);
      ToGeometry(mesh, &out_geo);
    }

    scene.geometries.emplace(surface_idx, std::move(out_geo));
  }

  return scene;
}

}  // namespace ioq3_map
