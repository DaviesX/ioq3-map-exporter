#include "scene.h"

#include <glog/logging.h>

#include <cmath>
#include <numbers>
#include <variant>

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
  // Scale (inches to meters). 1 inch = 0.0254 meters.
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

// Convert extracted BSPMesh to Scene Geometry (with coordinate transforms)
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
    const std::unordered_map<BSPSurfaceIndex, BSPGeometry>& bsp_geometries,
    const std::unordered_map<BSPTextureIndex, BSPMaterial>& bsp_materials) {
  Scene scene;

  // 1. Process Materials & Sun
  for (const auto& [id, bsp_mat] : bsp_materials) {
    Material mat;
    mat.name = bsp_mat.name;

    // Albedo
    if (!bsp_mat.texture_layers.empty()) {
      mat.albedo.file_path = bsp_mat.texture_layers[0].path;
    } else {
      mat.albedo.file_path = bsp_mat.name;
    }

    // Emission
    mat.emission_intensity = bsp_mat.q3map_surfacelight;

    scene.materials[id] = std::move(mat);

    // Sun check
    if (bsp_mat.q3map_sun_intensity > 0.0f) {
      // Check if we already have a sun
      bool sun_exists = false;
      for (const auto& l : scene.lights) {
        if (l.type == Light::Type::Directional) {
          sun_exists = true;
          break;
        }
      }

      if (!sun_exists) {
        Light sun;
        sun.type = Light::Type::Directional;
        // q3map_sun color is usually 0-1 or 0-255?
        // Specs say: "color is a vector of 3 floats". Examples show "1 0.9
        // 0.8". So likely normalized.
        sun.color = bsp_mat.q3map_sun_color;
        sun.intensity = bsp_mat.q3map_sun_intensity;

        // Calculate direction
        // q3map_sun <r> <g> <b> <intensity> <degrees> <elevation>
        // degrees: 0=East, 90=North
        // elevation: 0=Horizon, 90=Up
        // We want direction FROM light? Or TO light.
        // glTF directional light defines 'direction' as the direction the light
        // travels. Sun at specific angles implies light travels OPPOSITE to
        // that vector.

        // Convert to radians
        float yaw_deg = bsp_mat.q3map_sun_direction.x();
        float el_deg = bsp_mat.q3map_sun_direction.y();
        float yaw_rad = yaw_deg * std::numbers::pi_v<float> / 180.0f;
        float el_rad = el_deg * std::numbers::pi_v<float> / 180.0f;

        // Q3 vector to sun (Z-up)
        float cz = std::sin(el_rad);
        float r = std::cos(el_rad);
        float cx = r * std::cos(yaw_rad);
        float cy = r * std::sin(yaw_rad);

        Eigen::Vector3f q3_sun_pos(cx, cy, cz);
        // Light direction is -q3_sun_pos
        Eigen::Vector3f q3_light_dir = -q3_sun_pos;

        // Transform to glTF space (Y-up) using TransformNormal
        // Wait, TransformNormal rotates -90 X.
        // x' = x, y' = z, z' = -y
        sun.direction = TransformNormal(q3_light_dir);

        scene.lights.push_back(std::move(sun));
      }
    }
  }

  // 2. Process Geometries
  for (const auto& [surface_idx, geo] : bsp_geometries) {
    Geometry out_geo;
    out_geo.material_id = geo.texture_index;
    out_geo.transform = Eigen::Affine3f::Identity();

    // Triangulate / Convert
    bool valid_geo = false;
    if (std::holds_alternative<BSPPolygon>(geo.primitive)) {
      const auto& poly = std::get<BSPPolygon>(geo.primitive);
      BSPMesh mesh = Triangulate(poly);
      ToGeometry(mesh, &out_geo);
      valid_geo = true;
    } else if (std::holds_alternative<BSPMesh>(geo.primitive)) {
      const auto& mesh = std::get<BSPMesh>(geo.primitive);
      ToGeometry(mesh, &out_geo);
      valid_geo = true;
    } else if (std::holds_alternative<BSPPatch>(geo.primitive)) {
      const auto& patch = std::get<BSPPatch>(geo.primitive);
      BSPMesh mesh = Triangulate(patch);
      ToGeometry(mesh, &out_geo);
      valid_geo = true;
    }

    if (valid_geo) {
      scene.geometries.emplace(surface_idx, std::move(out_geo));

      // 3. Check for Area Light (Emissive Material)
      auto mat_it = scene.materials.find(geo.texture_index);
      if (mat_it != scene.materials.end() &&
          mat_it->second.emission_intensity > 0.0f) {
        Light area_light;
        area_light.type = Light::Type::Area;
        area_light.intensity = mat_it->second.emission_intensity;
        area_light.material_id = geo.texture_index;
        area_light.geometry_index = surface_idx;
        area_light.color =
            Eigen::Vector3f::Ones();  // Use material lightimage/color?
        // "q3map_lightimage": texture for color. "q3map_sunlight" or
        // "surfacelight" for intensity. For now use white, Phase 3
        // implementation says "extract... to match Q3 specs". If lightimage is
        // present, albedo might handle it or we need to extract average color.
        // Extracting average color from texture is complex (Image processing).
        // We'll leave color as white for now or rely on renderer to sample
        // texture.
        scene.lights.push_back(std::move(area_light));
      }
    }
  }

  return scene;
}

}  // namespace ioq3_map
