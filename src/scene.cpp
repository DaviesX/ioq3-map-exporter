#include "scene.h"

#include <unordered_map>

#include "bsp_geometry.h"

namespace ioq3_map {

namespace {

// Q3 is Z-up. glTF is Y-up.
// Standard conversion: Rotate -90 degrees around X axis.
// x' = x
// y' = z
// z' = -y
Eigen::Vector3f TransformPoint(const vec3_t& p) {
  // Scale (inches to meters)
  constexpr float kScale = 0.0254f;
  return Eigen::Vector3f(p[0] * kScale, p[2] * kScale, -p[1] * kScale);
}

Eigen::Vector3f TransformNormal(const vec3_t& n) {
  // Rotate -90 degrees around X axis.
  return Eigen::Vector3f(n[0], n[2], -n[1]);
}

Eigen::Vector2f TransformUV(const vec2_t& uv) {
  return Eigen::Vector2f(uv[0], uv[1]);
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

}  // namespace

Scene AssembleBSPObjects(
    const BSP& bsp,
    const std::unordered_map<BSPSurfaceIndex, BSPGeometry>& bsp_geometries) {
  Scene scene;

  // Load Shaders/Textures to create Materials
  size_t num_shaders = 0;
  const dshader_t* shaders =
      GetLumpData<dshader_t>(bsp, LumpType::Textures, num_shaders);

  // Map BSP Texture Index -> Scene Material Index
  std::unordered_map<int, int> texture_to_material_id;

  for (const auto& [surface_idx, geo] : bsp_geometries) {
    if (std::holds_alternative<BSPPatch>(geo.primitive)) {
      // TODO: Handle Patches (Tessellation)
      continue;
    }

    // Resolve Material
    int material_id = -1;
    if (texture_to_material_id.find(geo.texture_index) !=
        texture_to_material_id.end()) {
      material_id = texture_to_material_id[geo.texture_index];
    } else {
      material_id = scene.materials.size();
      Material mat;
      if (shaders && geo.texture_index >= 0 &&
          geo.texture_index < static_cast<int>(num_shaders)) {
        mat.name = shaders[geo.texture_index].shader;
        // Basic Albedo path (remove extension logic handled later?)
        mat.albedo.file_path = mat.name;
      } else {
        mat.name = "default_" + std::to_string(geo.texture_index);
      }
      scene.materials.push_back(std::move(mat));
      texture_to_material_id[geo.texture_index] = material_id;
    }

    Geometry out_geo;
    out_geo.material_id = material_id;
    out_geo.transform = Eigen::Affine3f::Identity();

    // Helper lambda to convert vertices and indices
    auto convert_mesh_data = [&](const auto& vertices, const auto& indices) {
      out_geo.vertices.reserve(vertices.size());
      out_geo.normals.reserve(vertices.size());
      out_geo.texture_uvs.reserve(vertices.size());
      out_geo.lightmap_uvs.reserve(vertices.size());

      for (const auto& v : vertices) {
        out_geo.vertices.push_back(TransformPoint(v.xyz));
        out_geo.normals.push_back(TransformNormal(v.normal));
        out_geo.texture_uvs.push_back(TransformUV(v.st));
        out_geo.lightmap_uvs.push_back(TransformUV(v.lightmap));
      }

      out_geo.indices.reserve(indices.size());
      for (int idx : indices) {
        out_geo.indices.push_back(static_cast<uint32_t>(idx));
      }
    };

    if (std::holds_alternative<BSPPolygon>(geo.primitive)) {
      const auto& poly = std::get<BSPPolygon>(geo.primitive);
      convert_mesh_data(poly.vertices, poly.indices);
    } else if (std::holds_alternative<BSPMesh>(geo.primitive)) {
      const auto& mesh = std::get<BSPMesh>(geo.primitive);
      convert_mesh_data(mesh.vertices, mesh.indices);
    }

    scene.geometries.push_back(std::move(out_geo));
  }

  return scene;
}

}  // namespace ioq3_map
