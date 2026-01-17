#ifndef IOQ3_MAP_SCENE_H_
#define IOQ3_MAP_SCENE_H_

#include <Eigen/Dense>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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
  Texture normal_texture;
  Texture metallic_roughness_texture;  // Metallic in B, Roughness in G

  // Emission (for Area Lights).
  float emission_intensity = 0.0f;
};

// --- Geometry ---
struct Geometry {
  std::vector<Eigen::Vector3f> vertices;
  std::vector<Eigen::Vector3f> normals;
  std::vector<Eigen::Vector2f> texture_uvs;
  std::vector<Eigen::Vector2f> lightmap_uvs;

  std::vector<uint32_t> indices;

  int material_id = -1;  // Index into Scene::materials
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
  const Material* material = nullptr;
  const Geometry* geometry = nullptr;
  int geometry_index = -1;  // For internal use: index into Scene::geometries.
};

// --- Sky ---
struct Sky {
  // For Texture type (HDRi)
  Texture texture;
  float intensity_multiplier = 1.0f;
};

// --- Scene ---
struct Scene {
  std::vector<Geometry> geometries;
  std::vector<Material> materials;
  std::vector<Light> lights;
  std::optional<Sky> sky;
};

}  // namespace ioq3_map

#endif  // IOQ3_MAP_SCENE_H_
