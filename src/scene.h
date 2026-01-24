#ifndef IOQ3_MAP_SCENE_H_
#define IOQ3_MAP_SCENE_H_

#include <Eigen/Dense>  // IWYU pragma: keep
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "bsp_entity.h"
#include "bsp_geometry.h"
#include "bsp_material.h"

namespace ioq3_map {

// --- Texture ---
struct Texture {
  std::filesystem::path file_path;
};

// --- Material ---
struct Material {
  std::string name;

  // Albedo / Transparency
  Texture albedo;

  // Emission Texture
  // If the shader provides a q3map_lightimage, it is used here.
  // Otherwise, if the material is emissive, we might reuse the albedo.
  Texture emission;

  // Emission (for Area Lights).
  float emission_intensity = 0.0f;
};

// --- Geometry/Triangle Mesh ---
struct Geometry {
  std::vector<Eigen::Vector3f> vertices;
  std::vector<Eigen::Vector3f> normals;
  std::vector<Eigen::Vector2f> texture_uvs;
  std::vector<Eigen::Vector2f> lightmap_uvs;

  std::vector<uint32_t> indices;

  BSPTextureIndex material_id = -1;
  Eigen::Affine3f transform = Eigen::Affine3f::Identity();
};

// --- Light ---
struct Light {
  enum class Type { Point, Directional, Spot, Area };
  Type type;

  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  Eigen::Vector3f direction = Eigen::Vector3f(0, 0, -1);
  Eigen::Vector3f color = Eigen::Vector3f::Ones();
  float intensity = 1.0f;

  float cos_inner_cone = 1.0f;
  float cos_outer_cone = 0.70710678118654752440f;  // cos(pi/4)

  // Area Light Parameters
  float area = 0.0f;
  BSPTextureIndex material_id = -1;
  BSPSurfaceIndex geometry_index = -1;
};

// --- Sky ---
struct Sky {
  Texture texture;
  float intensity_multiplier = 1.0f;
};

// --- Scene ---
struct Scene {
  std::unordered_map<BSPSurfaceIndex, Geometry> geometries;
  std::unordered_map<BSPTextureIndex, Material> materials;
  std::vector<Light> lights;
  std::optional<Sky> sky;
};

Scene AssembleBSPObjects(
    const BSP& bsp,
    const std::unordered_map<BSPSurfaceIndex, BSPGeometry>& bsp_geometries,
    const std::unordered_map<BSPTextureIndex, BSPMaterial>& bsp_materials,
    const std::vector<Entity>& bsp_entities);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_SCENE_H_
