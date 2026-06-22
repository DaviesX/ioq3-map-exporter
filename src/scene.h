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
  bool black_as_alpha = false;
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

  // Q3 texture layers for custom glTF extension
  std::vector<Q3TextureLayer> texture_layers;
  int surface_flags = 0;
  Q3CullType cull = Q3CullType::FRONT;
};

// Parameters controlling wall solidification (see extrude.h). Distances are in
// meters (glTF space). A zero thickness disables solidification.
struct ExtrusionConfig {
  float thickness = 0.0f;
  float inset = 0.0f;
};

// Options for AssembleBSPObjects.
struct AssemblyConfig {
  // Collect point/spot lights from entities. These are typically virtual lights
  // for artistic control; disable for physical correctness.
  bool collect_point_or_spot_lights = false;

  // Solidify thin opaque surfaces into closed shells.
  ExtrusionConfig extrusion;
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

// The transform emitted once on the "Worldspawn" root node: the Z-up -> Y-up
// axis conversion (a -90 degrees rotation about X). Geometry and lights are
// exported in the Quake 3 Z-up convention (Blender-friendly) and scaled to
// meters; this single root rotation completes the conversion to glTF Y-up, so
// downstream loaders that compose the node hierarchy reproduce the original
// world coordinates. Per-surface meshes additionally carry their own local
// origin on the geometry node (see RecenterToLocalOrigin).
Eigen::Affine3f Q3ToGltfRootTransform();

// When `config.extrusion.thickness > 0`, opaque single-sided surfaces are
// solidified into closed shells (see extrude.h). The default config disables
// both light collection and solidification.
Scene AssembleBSPObjects(
    const BSP& bsp,
    const std::unordered_map<BSPSurfaceIndex, BSPGeometry>& bsp_geometries,
    const std::unordered_map<BSPTextureIndex, BSPMaterial>& bsp_materials,
    const std::vector<Entity>& bsp_entities, const AssemblyConfig& config = {});

}  // namespace ioq3_map

#endif  // IOQ3_MAP_SCENE_H_
