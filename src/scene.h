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
  // Index into `texture_layers` of the layer whose texture was chosen as the
  // albedo source (the modern `_albedo` map replaces this layer at load time).
  // Emitted as `baseLayer` in SH_material_layers so consumers can substitute the
  // modern albedo into the correct layer when compositing.
  int albedo_layer = 0;
  int surface_flags = 0;
  Q3CullType cull = Q3CullType::FRONT;
};

// Parameters controlling wall solidification (see extrude.h). Distances are in
// meters (glTF space). A zero thickness disables solidification.
struct ExtrusionConfig {
  // Desired (maximum) shell thickness, in meters. The depth pass tries to
  // extrude every vertex this far and the spatially-aware clamp only shrinks it
  // where geometry is close behind the wall (see below). A zero thickness
  // disables solidification.
  float thickness = 0.2f;
  // Safety gap (meters) the shell tries to leave between its back cap and the
  // nearest surface behind the wall. The shell thickness is clamped to
  // `backward_clearance - clearance_margin` so it stops this far short of
  // whatever is behind it (R2 in the design note). A surface with no room to
  // clear this gap is not shelled at all. Only takes effect when solidification
  // is given a spatial-query callback.
  float clearance_margin = 0.01f;
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

  // Occluder-only shells (produced by solidification, see extrude.h) block light
  // transport in the baker's BVH and the renderer's shadow pass, but never
  // receive a lightmap chart and never render in the color pass. Visible
  // surfaces leave this false. Carried into glTF as the SH_occluder extension.
  bool occluder_only = false;
  // For occluder shells: the BSP surface index this shell was extruded from,
  // used only to name the glTF node (`Geometry_<source>_shell`) for debugging.
  // -1 on real (non-shell) geometry.
  BSPSurfaceIndex source_surface = -1;
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
  // Independent occluder-only shells, one per solidified surface. Kept separate
  // from `geometries` (which is keyed 1:1 by real BSP surface index) because
  // shells are synthetic and have no BSP face of their own.
  std::vector<Geometry> occluder_shells;
  std::unordered_map<BSPTextureIndex, Material> materials;
  std::vector<Light> lights;
  std::optional<Sky> sky;
};

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
