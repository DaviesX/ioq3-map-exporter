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

  // This is because Quake3 uses a clockwise winding order whereas OpenGL
  // uses counter-clockwise. So we insert indices in reverse order.
  out_geometry->indices.reserve(mesh.indices.size());
  for (int i = int(mesh.indices.size()) - 1; i >= 0; --i) {
    out_geometry->indices.push_back(static_cast<uint32_t>(mesh.indices[i]));
  }
}

}  // namespace

Scene AssembleBSPObjects(
    const BSP& bsp,
    const std::unordered_map<BSPSurfaceIndex, BSPGeometry>& bsp_geometries,
    const std::unordered_map<BSPTextureIndex, BSPMaterial>& bsp_materials,
    const std::vector<Entity>& bsp_entities) {
  Scene scene;

  // 1. Process Entities (Lights)
  for (const auto& entity : bsp_entities) {
    if (std::holds_alternative<PointLightEntity>(entity.data)) {
      const auto& p = std::get<PointLightEntity>(entity.data);
      Light light;
      light.type = Light::Type::Point;
      light.position = TransformPoint(p.origin);
      light.color = p.color;
      light.intensity = p.intensity;
      scene.lights.push_back(std::move(light));
    } else if (std::holds_alternative<SpotLightEntity>(entity.data)) {
      const auto& s = std::get<SpotLightEntity>(entity.data);
      Light light;
      light.type = Light::Type::Spot;
      light.position = TransformPoint(s.origin);
      light.direction = TransformNormal(s.direction);
      light.color = s.color;
      light.intensity = s.intensity;

      // Convert degrees to radians
      // GlTF uses half-angle in radians
      float outer_rad =
          (s.spot_angle / 2.0f) * std::numbers::pi_v<float> / 180.0f;
      light.cos_outer_cone = std::cos(outer_rad);

      // Inner cone relative to outer (e.g., 80%)
      float inner_rad = outer_rad * 0.8f;
      light.cos_inner_cone = std::cos(inner_rad);

      scene.lights.push_back(std::move(light));
    } else if (std::holds_alternative<
                   std::unordered_map<std::string, std::string>>(entity.data)) {
      const auto& props =
          std::get<std::unordered_map<std::string, std::string>>(entity.data);
      auto it_class = props.find("classname");
      if (it_class != props.end() && it_class->second == "worldspawn") {
        // Check for sun
        if (props.find("_sunlight") != props.end()) {
          Light sun;
          sun.type = Light::Type::Directional;

          // Intensity
          try {
            sun.intensity = std::stof(props.at("_sunlight"));
          } catch (...) {
            sun.intensity = 1.0f;
          }

          // Color
          if (props.find("_sunlight_color") != props.end()) {
            std::stringstream ss(props.at("_sunlight_color"));
            float r, g, b;
            ss >> r >> g >> b;
            // Assuming 0-255 inputs from typical Entity fields, but check range
            if (r > 1.0f || g > 1.0f || b > 1.0f) {
              sun.color = Eigen::Vector3f(r / 255.0f, g / 255.0f, b / 255.0f);
            } else {
              sun.color = Eigen::Vector3f(r, g, b);
            }
          }

          // Direction (_sun_mangle: Yaw Pitch Roll)
          if (props.find("_sun_mangle") != props.end()) {
            std::stringstream ss(props.at("_sun_mangle"));
            float yaw_deg, pitch_deg, roll_deg;
            ss >> yaw_deg >> pitch_deg >> roll_deg;

            // Q3 Angles to Vector
            // Yaw (around Z), Pitch (around Y/X?), Roll (around X)
            // _sun_mangle usage seems to be Yaw Pitch Roll
            // Q3 coords: X=Forward, Y=Left, Z=Up
            float yaw_rad = yaw_deg * std::numbers::pi_v<float> / 180.0f;
            float pitch_rad = pitch_deg * std::numbers::pi_v<float> / 180.0f;

            // Assuming pitch 0 is horizon, -90 is down (or up?)
            // Standard math:
            // x = cos(yaw) * cos(pitch)
            // y = sin(yaw) * cos(pitch)
            // z = sin(pitch)
            // But Q3 pitch is often inverted.
            // Let's rely on standard spherical coords for now.

            float cp = std::cos(pitch_rad);
            float sp = std::sin(pitch_rad);
            float cy = std::cos(yaw_rad);
            float sy = std::sin(yaw_rad);

            Eigen::Vector3f sun_dir(cp * cy, cp * sy, sp);
            // This is vector "to" sun? Or "from" sun?
            // "Direction of sun" usually means "where the sun IS".
            // Light direction is "where light GOES".
            // So light_dir = -sun_dir.
            Eigen::Vector3f light_dir = -sun_dir;

            sun.direction = TransformNormal(light_dir);
          }

          scene.lights.push_back(std::move(sun));
        }
      }
    }
  }

  // 2. Process Materials & Sun (Shader fallback)
  for (const auto& [id, bsp_mat] : bsp_materials) {
    Material mat;
    mat.name = bsp_mat.name;

    // Albedo
    if (bsp_mat.texture_layers.empty()) {
      LOG(WARNING) << "Material " << bsp_mat.name << " has no texture layers";
      continue;
    }
    // TODO: Support multiple texture layers.
    for (const auto& layer : bsp_mat.texture_layers) {
      if (std::holds_alternative<Q3TCModNoOp>(layer.tcmod)) {
        mat.albedo.file_path = layer.path;
      } else {
        // TODO: Implement tcmod. This links to the multi-texture support. We
        // will skip this for now.
      }
    }

    // Emission
    mat.emission_intensity = bsp_mat.q3map_surfacelight;

    scene.materials[id] = std::move(mat);

    // Sun check
    if (bsp_mat.q3map_sun_intensity > 0.0f) {
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

  // 3. Process Geometries
  for (const auto& [surface_idx, geo] : bsp_geometries) {
    Geometry out_geo;
    out_geo.material_id = geo.texture_index;
    out_geo.transform = Eigen::Affine3f::Identity();

    // Triangulate / Convert
    if (std::holds_alternative<BSPPolygon>(geo.primitive)) {
      const auto& poly = std::get<BSPPolygon>(geo.primitive);
      BSPMesh mesh = Triangulate(poly);
      ToGeometry(mesh, &out_geo);
    } else if (std::holds_alternative<BSPMesh>(geo.primitive)) {
      const auto& mesh = std::get<BSPMesh>(geo.primitive);
      ToGeometry(mesh, &out_geo);
    } else if (std::holds_alternative<BSPPatch>(geo.primitive)) {
      const auto& patch = std::get<BSPPatch>(geo.primitive);
      BSPMesh mesh = Triangulate(patch);
      ToGeometry(mesh, &out_geo);
    } else {
      LOG(ERROR) << "Unknown primitive type: " << geo.primitive.index();
      continue;
    }

    scene.geometries.emplace(surface_idx, std::move(out_geo));

    // 4. Check for Area Light (Emissive Material)
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

  return scene;
}

}  // namespace ioq3_map
