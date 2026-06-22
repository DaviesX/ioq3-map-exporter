#include "scene.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bsp_geometry.h"
#include "shader_parser.h"

namespace ioq3_map {
namespace {

// Helper to match vectors with tolerance
MATCHER_P(VectorNear, tol, "") {
  return (std::get<0>(arg) - std::get<1>(arg)).norm() < tol;
}

class SceneTest : public ::testing::Test {
 protected:
  BSP bsp_;
  std::unordered_map<BSPSurfaceIndex, BSPGeometry> geometries_;
  std::unordered_map<BSPTextureIndex, BSPMaterial> materials_;
  std::vector<Entity> entities_;
};

TEST_F(SceneTest, AssembleBSPObjectsPlanarTransform) {
  // ... (setup code omitted, assumed unchanged up to the call) ...
  // Q3 Coords: triangle at Z=0
  // v0=(0,0,0), v1=(100,0,0), v2=(0,100,0) normal=(0,0,1)
  BSPGeometry geo;
  geo.texture_index = 0;
  BSPPolygon poly;
  vertex_t v0;
  v0.xyz = {0, 0, 0};
  v0.normal = {0, 0, 1};
  vertex_t v1;
  v1.xyz = {100, 0, 0};
  v1.normal = {0, 0, 1};
  vertex_t v2;
  v2.xyz = {0, 100, 0};
  v2.normal = {0, 0, 1};
  poly.vertices = {v0, v1, v2};
  poly.indices = {0, 1, 2};
  geo.primitive = poly;
  geometries_[0] = geo;

  // Setup Material
  BSPMaterial mat;
  mat.name = "textures/base_wall/concrete";
  mat.texture_layers.push_back(Q3TextureLayer{
      .path = "./vfs_mount_point/textures/base_wall/concrete.tga"});
  materials_[0] = mat;

  Scene scene =
      AssembleBSPObjects(bsp_, geometries_, materials_, entities_,
                         {.collect_point_or_spot_lights = true});

  ASSERT_EQ(scene.geometries.size(), 1);
  const auto& out_geo = scene.geometries.at(0);

  // Geometry is kept in the Quake 3 Z-up convention and scaled to meters, then
  // re-centered to its own local origin (the AABB center, recorded on the node
  // transform). The Z-up -> Y-up conversion lives on the root node.
  constexpr float kScale = 0.0254f;

  // Scaled (absolute) Z-up positions are (0,0,0), (2.54,0,0), (0,2.54,0), so the
  // AABB center / local origin is (1.27, 1.27, 0).
  Eigen::Vector3f expected_origin(50 * kScale, 50 * kScale, 0.0f);
  EXPECT_TRUE(out_geo.transform.translation().isApprox(expected_origin, 1e-4f))
      << out_geo.transform.translation().transpose();

  // node_transform * local_vertex recovers the absolute Z-up (meters) position.
  Eigen::Vector3f w0 = out_geo.transform * out_geo.vertices[0];
  Eigen::Vector3f w1 = out_geo.transform * out_geo.vertices[1];
  Eigen::Vector3f w2 = out_geo.transform * out_geo.vertices[2];
  EXPECT_NEAR(w0.x(), 0.0f, 1e-5f);
  EXPECT_NEAR(w0.y(), 0.0f, 1e-5f);
  EXPECT_NEAR(w1.x(), 100 * kScale, 1e-5f);
  EXPECT_NEAR(w1.y(), 0.0f, 1e-5f);
  EXPECT_NEAR(w2.x(), 0.0f, 1e-5f);
  EXPECT_NEAR(w2.y(), 100 * kScale, 1e-5f);

  // Composing the root transform reproduces the legacy glTF Y-up world coords
  // (x'=x, y'=z, z'=-y).
  Eigen::Affine3f root = Q3ToGltfRootTransform();
  Eigen::Vector3f g1 = root * w1;  // (2.54, 0, 0) -> (2.54, 0, 0)
  EXPECT_NEAR(g1.x(), 100 * kScale, 1e-5f);
  EXPECT_NEAR(g1.y(), 0.0f, 1e-5f);
  EXPECT_NEAR(g1.z(), 0.0f, 1e-5f);
  Eigen::Vector3f g2 = root * w2;  // (0, 2.54, 0) -> (0, 0, -2.54)
  EXPECT_NEAR(g2.z(), -100 * kScale, 1e-5f);

  // Normal stays Z-up (0,0,1); the root rotates it to Y-up (0,1,0).
  EXPECT_NEAR(out_geo.normals[0].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.normals[0].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.normals[0].z(), 1.0f, 1e-5f);
  Eigen::Vector3f gn = root.rotation() * out_geo.normals[0];
  EXPECT_NEAR(gn.y(), 1.0f, 1e-5f);

  // Material Check
  ASSERT_EQ(scene.materials.size(), 1);
  EXPECT_EQ(scene.materials.at(0).name, "textures/base_wall/concrete");
  EXPECT_EQ(out_geo.material_id, 0);
}

TEST_F(SceneTest, AssembleBSPObjectsExtractsSun) {
  BSPMaterial mat;
  mat.name = "textures/skies/sky_sun";
  mat.surface_flags = SURF_SKY;
  mat.q3map_sun_intensity = 100.0f;
  mat.q3map_sun_color = Eigen::Vector3f(1.0f, 1.0f, 1.0f);
  mat.q3map_sun_direction = Eigen::Vector2f(90.0f, 45.0f);  // North, 45deg up
  // Add a dummy texture layer to pass the validation check
  mat.texture_layers.push_back(
      Q3TextureLayer{.path = "textures/skies/sky_sun.tga"});
  materials_[0] = mat;

  Scene scene =
      AssembleBSPObjects(bsp_, geometries_, materials_, entities_,
                         {.collect_point_or_spot_lights = true});

  bool found_sun = false;
  for (const auto& l : scene.lights) {
    if (l.type == Light::Type::Directional) {
      found_sun = true;
      EXPECT_FLOAT_EQ(l.intensity, 100.0f);
      // Verify direction
      // Yaw 90 (North), El 45
      // Q3: x=0, y=r, z=sin(45)
      // r = cos(45) = 0.707
      // y = 0.707 * sin(90) = 0.707
      // z = 0.707
      // Q3 Sun Pos: (0, 0.707, 0.707)
      // Direction is now kept in Quake 3 Z-up (not pre-rotated):
      // Q3 Light Dir: (0, -0.707, -0.707)
      EXPECT_NEAR(l.direction.x(), 0.0f, 1e-3f);
      EXPECT_NEAR(l.direction.y(), -0.707f, 1e-3f);
      EXPECT_NEAR(l.direction.z(), -0.707f, 1e-3f);

      // Composing the root rotation yields the legacy glTF Y-up direction
      // (x'=x, y'=z, z'=-y) -> (0, -0.707, 0.707).
      Eigen::Vector3f gltf_dir =
          Q3ToGltfRootTransform().rotation() * l.direction;
      EXPECT_NEAR(gltf_dir.x(), 0.0f, 1e-3f);
      EXPECT_NEAR(gltf_dir.y(), -0.707f, 1e-3f);
      EXPECT_NEAR(gltf_dir.z(), 0.707f, 1e-3f);
    }
  }
  EXPECT_TRUE(found_sun);
}

TEST_F(SceneTest, AssembleBSPObjectsExtractsEntities) {
  // Point Light
  PointLightEntity point;
  point.origin = Eigen::Vector3f(100, 200, 300);
  point.color = Eigen::Vector3f(1, 0, 0);
  point.intensity = 500;
  Entity e1;
  e1.data = point;
  entities_.push_back(e1);

  // Spot Light
  SpotLightEntity spot;
  spot.origin = Eigen::Vector3f(0, 0, 0);
  spot.direction = Eigen::Vector3f(0, 0, -1);
  spot.color = Eigen::Vector3f(0, 1, 0);
  spot.intensity = 200;
  spot.spot_angle = 60.0f;
  Entity e2;
  e2.data = spot;
  entities_.push_back(e2);

  Scene scene =
      AssembleBSPObjects(bsp_, geometries_, materials_, entities_,
                         {.collect_point_or_spot_lights = true});

  EXPECT_GE(scene.lights.size(), 2);

  // Verify Point
  bool found_point = false;
  for (const auto& l : scene.lights) {
    if (l.type == Light::Type::Point && l.intensity == 500.0f) {
      found_point = true;
      // Position is Quake 3 Z-up scaled to meters: (100,200,300) in -> meters.
      EXPECT_NEAR(l.position.x(), 100.0f * 0.0254f, 1e-3f);
      EXPECT_NEAR(l.position.y(), 200.0f * 0.0254f, 1e-3f);
      EXPECT_NEAR(l.position.z(), 300.0f * 0.0254f, 1e-3f);
      // Composing the root transform reproduces the legacy glTF Y-up position
      // (x'=x, y'=z, z'=-y).
      Eigen::Vector3f gltf_pos = Q3ToGltfRootTransform() * l.position;
      EXPECT_NEAR(gltf_pos.x(), 100.0f * 0.0254f, 1e-3f);
      EXPECT_NEAR(gltf_pos.y(), 300.0f * 0.0254f, 1e-3f);
      EXPECT_NEAR(gltf_pos.z(), -200.0f * 0.0254f, 1e-3f);
    }
  }
  EXPECT_TRUE(found_point);

  // Verify Spot
  bool found_spot = false;
  for (const auto& l : scene.lights) {
    if (l.type == Light::Type::Spot && l.intensity == 200.0f) {
      found_spot = true;
      // Check angles. Inner = 0.8 * Outer. Outer = 30 deg rads.
      float outer_rad = 30.0f * std::numbers::pi_v<float> / 180.0f;
      float inner_rad = outer_rad * 0.8f;
      EXPECT_NEAR(l.cos_outer_cone, std::cos(outer_rad), 1e-4f);
      EXPECT_NEAR(l.cos_inner_cone, std::cos(inner_rad), 1e-4f);
    }
  }
  EXPECT_TRUE(found_spot);
}

}  // namespace
}  // namespace ioq3_map
