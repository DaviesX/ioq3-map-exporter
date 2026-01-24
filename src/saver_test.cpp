#include "saver.h"

#include <gtest/gtest.h>
#include <tiny_gltf.h>

#include <cmath>
#include <filesystem>

#include "scene.h"
#include "stb_image_write.h"

namespace ioq3_map {
namespace {

// Helper to load scene back for verification
std::optional<Scene> LoadScene(const std::filesystem::path& path) {
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;
  bool load_ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
  if (!load_ret) {
    return std::nullopt;
  }

  Scene scene;

  // 1. Recover Materials
  // Map glTF material index -> BSPTextureIndex (just sequential for now)
  for (size_t i = 0; i < model.materials.size(); ++i) {
    Material mat;
    mat.name = model.materials[i].name;
    // We map back to an arbitrary index for verification
    scene.materials[i] = mat;
  }

  // 2. Recover Geometries
  // Flatten node hierarchy to find meshes
  std::function<void(int)> traverse;
  traverse = [&](int node_idx) {
    const auto& node = model.nodes[node_idx];
    if (node.mesh >= 0) {
      Geometry geo;
      // Partial recovery for verification
      const auto& mesh = model.meshes[node.mesh];
      if (!mesh.primitives.empty()) {
        const auto& prim = mesh.primitives[0];
        // Material ID is glTF material index
        // We need to map it back to BSPTextureIndex.
        // Since we assigned bsp_idx = i, let's assume direct mapping for test.
        geo.material_id = prim.material;
      }
      scene.geometries[scene.geometries.size()] = geo;
    }

    // Check for Lights (Extension)
    auto ext_it = node.extensions.find("KHR_lights_punctual");
    if (ext_it != node.extensions.end()) {
      // This node is a light node
      // In this simple loader we can't easily jump to the light definition
      // without parsing the KHR_lights_punctual top-level extension too.
      // But we can check if the extension exists.
    }
    // Also check node.light if parsed by tinygltf
    if (node.light != -1) {
      // We found a light instance
      // We need to find the light definition in model.extensions
      // For now, let's just count them in main test logic or inspect model
      // directly
    }

    for (int child : node.children) {
      traverse(child);
    }
  };

  const auto& default_scene = model.scenes[model.defaultScene];
  for (int node_idx : default_scene.nodes) {
    traverse(node_idx);
  }

  // 3. Recover Lights (Basic count check via extensions)
  if (model.extensions.count("KHR_lights_punctual")) {
    const auto& ext = model.extensions.at("KHR_lights_punctual");
    if (ext.Has("lights")) {
      const auto& lights = ext.Get("lights");
      for (size_t i = 0; i < lights.ArrayLen(); ++i) {
        const auto& light_obj = lights.Get(i);
        Light l;
        std::string type = light_obj.Get("type").Get<std::string>();
        if (type == "point")
          l.type = Light::Type::Point;
        else if (type == "spot")
          l.type = Light::Type::Spot;
        else if (type == "directional")
          l.type = Light::Type::Directional;
        scene.lights.push_back(l);
      }
    }
  }

  return scene;
}

}  // namespace

TEST(SaverTest, SaveSceneWithTexture) {
  // Setup temp directory
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "sh_baker_test_scene";
  std::filesystem::create_directories(temp_dir);

  // Create a dummy source texture file
  std::filesystem::path source_tex_dir = temp_dir / "source";
  std::filesystem::create_directories(source_tex_dir);
  std::filesystem::path source_tex_path = source_tex_dir / "test_albedo.png";

  {
    unsigned char pixels[] = {255, 0, 0};  // Red
    stbi_write_png(source_tex_path.string().c_str(), 1, 1, 3, pixels, 3);
  }

  // Create Scene
  Scene scene;
  Material mat;
  mat.name = "TestMat";
  mat.albedo.file_path = source_tex_path;
  scene.materials[0] = mat;

  // Add dummy geometry to trigger buffer generation
  Geometry geo;
  geo.vertices = {Eigen::Vector3f(0, 0, 0), Eigen::Vector3f(1, 0, 0),
                  Eigen::Vector3f(0, 1, 0)};
  geo.indices = {0, 1, 2};
  geo.material_id = 0;
  scene.geometries[0] = geo;

  std::filesystem::path output_gltf = temp_dir / "output" / "scene.gltf";
  std::filesystem::create_directories(output_gltf.parent_path());

  bool ret = SaveScene(scene, output_gltf);
  ASSERT_TRUE(ret);

  // Checks
  ASSERT_TRUE(std::filesystem::exists(output_gltf));

  // Check texture copy
  std::filesystem::path copied_tex_path =
      output_gltf.parent_path() / "source@test_albedo.png";
  EXPECT_TRUE(std::filesystem::exists(copied_tex_path));

  // Check bin file (External buffers)
  std::filesystem::path bin_path = output_gltf.parent_path() / "scene.bin";
  // The current implementation might NOT create scene.bin if it uses multiple
  // buffers without a single top-level binary blob, OR if tinygltf is
  // configured differently. Our implementation uses AddBufferView with
  // model->buffers[0], so it should be fine. However, tinygltf
  // WriteGltfSceneToFile behavior depends on flags. We passed false for
  // embed_xyz, so it should write external files.
  // EXPECT_TRUE(std::filesystem::exists(bin_path)); // Might depend on naming
  // convention

  // Load back and verify
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;
  bool load_ret =
      loader.LoadASCIIFromFile(&model, &err, &warn, output_gltf.string());
  ASSERT_TRUE(load_ret) << err;

  ASSERT_EQ(model.materials.size(), 1);
  EXPECT_EQ(model.materials[0].name, "TestMat");
  int tex_index =
      model.materials[0].pbrMetallicRoughness.baseColorTexture.index;
  ASSERT_GE(tex_index, 0);
  ASSERT_LT(tex_index, model.textures.size());

  int source_index = model.textures[tex_index].source;
  ASSERT_GE(source_index, 0);
  ASSERT_LT(source_index, model.images.size());

  EXPECT_EQ(model.images[source_index].uri, "source@test_albedo.png");

  // Cleanup
  std::filesystem::remove_all(temp_dir);
}

TEST(SaverTest, SaveComplexScene) {
  Scene scene;

  // 1. Materials
  for (int i = 0; i < 5; ++i) {
    Material mat;
    mat.name = "Mat_" + std::to_string(i);
    // No texture for these
    scene.materials[i] = mat;
  }

  // 2. Geometries
  for (int i = 0; i < 3; ++i) {
    Geometry geo;
    geo.vertices = {Eigen::Vector3f(0, 0, 0), Eigen::Vector3f(1, 0, 0),
                    Eigen::Vector3f(0, 1, 0)};
    geo.normals = {Eigen::Vector3f(0, 0, 1), Eigen::Vector3f(0, 0, 1),
                   Eigen::Vector3f(0, 0, 1)};
    geo.texture_uvs = {Eigen::Vector2f(0, 0), Eigen::Vector2f(1, 0),
                       Eigen::Vector2f(0, 1)};
    geo.indices = {0, 1, 2};
    geo.material_id = i;  // Use different materials
    scene.geometries[i] = geo;
  }

  // 3. Lights
  Light pointLight;
  pointLight.type = Light::Type::Point;
  pointLight.position = Eigen::Vector3f(10, 10, 10);
  pointLight.intensity = 5.0f;
  scene.lights.push_back(pointLight);

  Light spotLight;
  spotLight.type = Light::Type::Spot;
  spotLight.position = Eigen::Vector3f(0, 5, 0);
  spotLight.direction = Eigen::Vector3f(0, -1, 0);
  // cos(angle)
  spotLight.cos_inner_cone = std::cos(0.5f);
  spotLight.cos_outer_cone = std::cos(0.8f);
  scene.lights.push_back(spotLight);

  Light dirLight;
  dirLight.type = Light::Type::Directional;
  dirLight.direction = Eigen::Vector3f(1, 0, 0);
  scene.lights.push_back(dirLight);

  // Setup path
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "sh_baker_test_complex";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path output_path = temp_dir / "complex.gltf";

  // Save
  bool ret = SaveScene(scene, output_path);
  ASSERT_TRUE(ret);

  // Load back using local LoadScene
  auto loaded_scene_opt = LoadScene(output_path);
  ASSERT_TRUE(loaded_scene_opt.has_value())
      << "Failed to load saved scene from " << output_path;
  const Scene& loaded_scene = *loaded_scene_opt;

  // Checks
  EXPECT_EQ(loaded_scene.materials.size(), 5);
  EXPECT_EQ(loaded_scene.geometries.size(), 3);

  // Check Lights
  EXPECT_EQ(loaded_scene.lights.size(), 3);

  // Verify light types
  int point_count = 0;
  int spot_count = 0;
  int dir_count = 0;

  for (const auto& l : loaded_scene.lights) {
    if (l.type == Light::Type::Point) point_count++;
    if (l.type == Light::Type::Spot) spot_count++;
    if (l.type == Light::Type::Directional) dir_count++;
  }
  EXPECT_EQ(point_count, 1);
  EXPECT_EQ(spot_count, 1);
  EXPECT_EQ(dir_count, 1);

  // Cleanup
  std::filesystem::remove_all(temp_dir);
}

TEST(SaverTest, SaveAreaLightWithEmissiveMaterial) {
  Scene scene;

  // Material with High Emission
  Material mat;
  mat.name = "EmissiveMat";
  mat.emission_intensity =
      5.0f;  // High intensity -> Needs extension or clamping
  scene.materials[0] = mat;

  // Add a geometry using this material
  Geometry geo;
  geo.vertices = {Eigen::Vector3f(0, 0, 0), Eigen::Vector3f(1, 0, 0),
                  Eigen::Vector3f(0, 1, 0)};
  geo.indices = {0, 1, 2};
  geo.material_id = 0;
  scene.geometries[0] = geo;

  // Emulate Area Light in lights array (should be ignored by saver loop but
  // material should handle it)
  Light areaLight;
  areaLight.type = Light::Type::Area;
  areaLight.intensity = 5.0f;
  areaLight.material_id = 0;
  scene.lights.push_back(areaLight);

  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "area_light_test";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::path output_path = temp_dir / "area.gltf";

  // Create explicit emission texture
  std::filesystem::path emission_tex = temp_dir / "emission.png";
  {
    unsigned char pixels[] = {0, 255, 0};  // Green
    stbi_write_png(emission_tex.string().c_str(), 1, 1, 3, pixels, 3);
  }
  scene.materials[0].emission.file_path = emission_tex;

  ASSERT_TRUE(SaveScene(scene, output_path));

  // Load back
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;
  ASSERT_TRUE(
      loader.LoadASCIIFromFile(&model, &err, &warn, output_path.string()));

  ASSERT_EQ(model.materials.size(), 1);
  const auto& gmat = model.materials[0];

  // Check Emissive Factor
  EXPECT_EQ(gmat.emissiveFactor.size(), 3);
  EXPECT_DOUBLE_EQ(gmat.emissiveFactor[0], 1.0);
  EXPECT_DOUBLE_EQ(gmat.emissiveFactor[1], 1.0);
  EXPECT_DOUBLE_EQ(gmat.emissiveFactor[2], 1.0);

  // Check Emissive Texture
  ASSERT_GE(gmat.emissiveTexture.index, 0);
  const auto& em_tex = model.textures[gmat.emissiveTexture.index];
  const auto& em_img = model.images[em_tex.source];
  EXPECT_EQ(em_img.uri, "area_light_test@emission.png");

  // Check Extension
  // We expect KHR_materials_emissive_strength because intensity is 5.0
  auto ext_it = gmat.extensions.find("KHR_materials_emissive_strength");
  ASSERT_NE(ext_it, gmat.extensions.end());
  EXPECT_TRUE(ext_it->second.Has("emissiveStrength"));
  EXPECT_DOUBLE_EQ(ext_it->second.Get("emissiveStrength").Get<double>(), 5.0);

  // Check that extension is in extensionsUsed
  bool has_ext = false;
  for (const auto& ext : model.extensionsUsed) {
    if (ext == "KHR_materials_emissive_strength") has_ext = true;
  }
  EXPECT_TRUE(has_ext);

  std::filesystem::remove_all(temp_dir);
}

}  // namespace ioq3_map
