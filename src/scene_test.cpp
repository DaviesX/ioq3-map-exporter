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

  // Verify transform: x'=x, y'=z, z'=-y
  // v0': (0, 0, 0)
  // v1': (100*scale, 0, 0) -> (2.54, 0, 0)
  // v2': (0, 0, -100*scale) -> (0, 0, -2.54)
  // normal': (0, 1, 0)

  constexpr float kScale = 0.0254f;
  EXPECT_NEAR(out_geo.vertices[0].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[0].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[0].z(), 0.0f, 1e-5f);

  EXPECT_NEAR(out_geo.vertices[1].x(), 100 * kScale, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[1].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[1].z(), 0.0f, 1e-5f);

  EXPECT_NEAR(out_geo.vertices[2].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[2].y(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.vertices[2].z(), -100 * kScale, 1e-5f);  // z' = -y

  // Normal (0,0,1) -> (0,1,0)
  EXPECT_NEAR(out_geo.normals[0].x(), 0.0f, 1e-5f);
  EXPECT_NEAR(out_geo.normals[0].y(), 1.0f, 1e-5f);
  EXPECT_NEAR(out_geo.normals[0].z(), 0.0f, 1e-5f);

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
      // Q3 Light Dir: (0, -0.707, -0.707)
      // Transform (Rot -90 X):
      // x'=x=0
      // y'=z=-0.707
      // z'=-y=0.707
      EXPECT_NEAR(l.direction.x(), 0.0f, 1e-3f);
      EXPECT_NEAR(l.direction.y(), -0.707f, 1e-3f);
      EXPECT_NEAR(l.direction.z(), 0.707f, 1e-3f);
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
      // 100 in -> 2.54 m
      EXPECT_NEAR(l.position.x(), 2.54f, 1e-3f);
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

TEST_F(SceneTest, AssembleBSPObjectsPrefersOpaqueBaseEvenWithTcMod) {
  // A scrolling computer screen: layer 0 is the opaque diffuse base
  // (GL_ONE, GL_ZERO) that happens to scroll; layer 1 is an additive glow
  // overlay (GL_ONE, GL_ONE). The base albedo must be the opaque scrolling base
  // (index 0) -- its tcMod rides along in SH_material_layers -- never the
  // additive overlay.
  BSPMaterial mat;
  mat.name = "textures/sfx/screen";
  Q3TextureLayer base;
  base.path = "textures/sfx/screen_base.tga";  // GL_ONE/GL_ZERO opaque
  base.tcmod = Q3TCModScroll{0.f, 1.f};
  Q3TextureLayer glow;
  glow.path = "textures/sfx/screen_glow.tga";
  glow.blend_src = BlendFunc::ONE;
  glow.blend_dst = BlendFunc::ONE;  // additive
  mat.texture_layers = {base, glow};
  materials_[0] = mat;

  Scene scene = AssembleBSPObjects(bsp_, geometries_, materials_, entities_);

  ASSERT_EQ(scene.materials.count(0), 1u);
  EXPECT_EQ(scene.materials.at(0).albedo_layer, 0);
  EXPECT_EQ(scene.materials.at(0).albedo.file_path,
            "textures/sfx/screen_base.tga");
}

TEST_F(SceneTest, AssembleBSPObjectsFullyAdditiveTagsLayerZero) {
  // A flame: every stage is additive (GL_ONE, GL_ONE) with no opaque or
  // alpha-blended base. There is no meaningful diffuse, so baseLayer falls back
  // to 0 (the consumer treats the whole stack as additive).
  BSPMaterial mat;
  mat.name = "textures/sfx/flame";
  auto additive = [](const char* p) {
    Q3TextureLayer l;
    l.path = p;
    l.blend_src = BlendFunc::ONE;
    l.blend_dst = BlendFunc::ONE;
    return l;
  };
  mat.texture_layers = {additive("textures/sfx/flame1.tga"),
                        additive("textures/sfx/flame2.tga"),
                        additive("textures/sfx/flameball.tga")};
  materials_[0] = mat;

  Scene scene = AssembleBSPObjects(bsp_, geometries_, materials_, entities_);

  ASSERT_EQ(scene.materials.count(0), 1u);
  EXPECT_EQ(scene.materials.at(0).albedo_layer, 0);
}

TEST_F(SceneTest, AssembleBSPObjectsFallbackAvoidsAdditiveOverlay) {
  // No opaque stage: layer 0 is an additive glow, layer 1 is an alpha-blended
  // base. baseLayer must skip the additive overlay and pick the blended base
  // (index 1), not layer 0.
  BSPMaterial mat;
  mat.name = "textures/sfx/glasspanel";
  Q3TextureLayer glow;
  glow.path = "textures/sfx/glow.tga";
  glow.blend_src = BlendFunc::ONE;
  glow.blend_dst = BlendFunc::ONE;  // additive overlay
  Q3TextureLayer blended;
  blended.path = "textures/sfx/glass.tga";
  blended.blend_src = BlendFunc::SRC_ALPHA;
  blended.blend_dst = BlendFunc::ONE_MINUS_SRC_ALPHA;  // translucent base
  mat.texture_layers = {glow, blended};
  materials_[0] = mat;

  Scene scene = AssembleBSPObjects(bsp_, geometries_, materials_, entities_);

  ASSERT_EQ(scene.materials.count(0), 1u);
  EXPECT_EQ(scene.materials.at(0).albedo_layer, 1);
}

TEST_F(SceneTest, AssembleBSPObjectsPrefersOpaqueBaseOverAdditiveOverlay) {
  // The compscreen case: an opaque diffuse panel (GL_ONE, GL_ZERO) at layer 0,
  // then an additive letters overlay (GL_ONE, GL_ONE) at layer 1. The base
  // albedo must be the opaque panel (index 0) -- picking the overlay (and
  // keying its black background to alpha) used to turn the wall into a cut-out
  // hole downstream.
  BSPMaterial mat;
  mat.name = "textures/sfx/compscreen";
  Q3TextureLayer base;
  base.path = "textures/sfx/panel.tga";  // GL_ONE/GL_ZERO opaque
  Q3TextureLayer overlay;
  overlay.path = "textures/sfx/letters.tga";
  overlay.blend_src = BlendFunc::ONE;
  overlay.blend_dst = BlendFunc::ONE;  // additive
  mat.texture_layers = {base, overlay};
  materials_[0] = mat;

  Scene scene = AssembleBSPObjects(bsp_, geometries_, materials_, entities_);

  ASSERT_EQ(scene.materials.count(0), 1u);
  EXPECT_EQ(scene.materials.at(0).albedo_layer, 0);
  EXPECT_EQ(scene.materials.at(0).albedo.file_path, "textures/sfx/panel.tga");
}

}  // namespace
}  // namespace ioq3_map
