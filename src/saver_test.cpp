#include "saver.h"

#include <gtest/gtest.h>
#include <tiny_gltf.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#include "scene.h"
#include "stb_image.h"
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
  // Expected structure: output/source@test_albedo/original_diffuse.png
  std::filesystem::path folder_name = "source@test_albedo";
  std::filesystem::path texture_dir = output_gltf.parent_path() / folder_name;

  EXPECT_TRUE(
      std::filesystem::exists(texture_dir / "source@test_albedo_diffuse.png"));
  EXPECT_TRUE(
      std::filesystem::exists(texture_dir / "source@test_albedo_albedo.png"));
  EXPECT_TRUE(
      std::filesystem::exists(texture_dir / "source@test_albedo_normal.png"));
  EXPECT_TRUE(
      std::filesystem::exists(texture_dir / "source@test_albedo_orm.png"));

  // Check bin file (External buffers)
  std::filesystem::path bin_path = output_gltf.parent_path() / "scene.bin";
  // EXPECT_TRUE(std::filesystem::exists(bin_path));

  // Check raw JSON content to verify URI is not stripped by writer
  std::ifstream f(output_gltf);
  std::stringstream buffer;
  buffer << f.rdbuf();
  std::string gltf_json = buffer.str();
  // We look for "source@test_albedo/source@test_albedo_albedo.png"
  std::string expected_uri =
      (folder_name / (folder_name.string() + "_albedo.png")).string();
  EXPECT_NE(gltf_json.find(expected_uri), std::string::npos)
      << "glTF JSON should contain full relative path: " << expected_uri
      << "\nContent:\n"
      << gltf_json;

  // Load back and verify
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;
  bool load_ret =
      loader.LoadASCIIFromFile(&model, &err, &warn, output_gltf.string());
  ASSERT_TRUE(load_ret) << err;

  ASSERT_EQ(model.materials.size(), 1);
  EXPECT_EQ(model.materials[0].name, "TestMat");

  // Verify links to placeholders
  // Base Color -> albedo.png
  int tex_index =
      model.materials[0].pbrMetallicRoughness.baseColorTexture.index;
  ASSERT_GE(tex_index, 0);
  ASSERT_LT(tex_index, model.textures.size());
  int source_index = model.textures[tex_index].source;
  ASSERT_GE(source_index, 0);
  EXPECT_EQ(model.images[source_index].uri,
            (folder_name / (folder_name.string() + "_albedo.png")).string());

  // MetallicRoughness -> orm.png
  tex_index =
      model.materials[0].pbrMetallicRoughness.metallicRoughnessTexture.index;
  ASSERT_GE(tex_index, 0);
  source_index = model.textures[tex_index].source;
  EXPECT_EQ(model.images[source_index].uri,
            (folder_name / (folder_name.string() + "_orm.png")).string());

  // Normal -> normal.png
  tex_index = model.materials[0].normalTexture.index;
  ASSERT_GE(tex_index, 0);
  source_index = model.textures[tex_index].source;
  EXPECT_EQ(model.images[source_index].uri,
            (folder_name / (folder_name.string() + "_normal.png")).string());

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
  // Expect folder structure
  std::filesystem::path folder_name = "area_light_test@emission";
  EXPECT_EQ(em_img.uri,
            (folder_name / (folder_name.string() + "_emissive.png")).string());

  // Check Extension
  // We expect KHR_materials_emissive_strength because intensity is 5.0
  auto ext_it = gmat.extensions.find("KHR_materials_emissive_strength");
  ASSERT_NE(ext_it, gmat.extensions.end());
  EXPECT_TRUE(ext_it->second.Has("emissiveStrength"));
  EXPECT_DOUBLE_EQ(ext_it->second.Get("emissiveStrength").Get<double>(),
                   5.0 / 2.0);

  // Check that extension is in extensionsUsed
  bool has_ext = false;
  for (const auto& ext : model.extensionsUsed) {
    if (ext == "KHR_materials_emissive_strength") has_ext = true;
  }
  EXPECT_TRUE(has_ext);

  std::filesystem::remove_all(temp_dir);
}

TEST(SaverTest, SaveWithBlackAlpha) {
  // Setup temp directory
  std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "black_alpha_test";
  std::filesystem::create_directories(temp_dir);

  // 1. Create source texture (2x2)
  // Pixels: Black (0,0,0), Red (255,0,0), Green (0,255,0), White (255,255,255)
  // Expected Alpha: 0, 255, 255, 255
  std::vector<unsigned char> pixels = {
      0,   0,   0,   // Black
      255, 0,   0,   // Red
      0,   255, 0,   // Green
      255, 255, 255  // White
  };

  std::filesystem::path source_tex =
      temp_dir / "test.jpg";  // Pretend it's a JPG/TGA
  // stbi_write_png is fine, we just need a file on disk.
  stbi_write_png(source_tex.string().c_str(), 2, 2, 3, pixels.data(), 2 * 3);

  // 2. Setup Scene
  Scene scene;
  Material mat;
  mat.name = "TransparencyMat";
  mat.albedo.file_path = source_tex;
  mat.albedo.black_as_alpha = true;
  scene.materials[0] = mat;

  // Geometry
  Geometry geo;
  geo.vertices = {Eigen::Vector3f(0, 0, 0), Eigen::Vector3f(1, 0, 0),
                  Eigen::Vector3f(0, 1, 0)};
  geo.indices = {0, 1, 2};
  geo.material_id = 0;
  scene.geometries[0] = geo;

  // 3. Save
  std::filesystem::path output_path = temp_dir / "out.gltf";
  ASSERT_TRUE(SaveScene(scene, output_path));

  // 4. Verify output file existence and extension
  // The saver should rename the texture to .png
  // 4. Verify output file existence and extension
  // The saver should create a folder and place original_diffuse.png inside
  std::filesystem::path folder_name = "black_alpha_test@test";
  std::filesystem::path expected_tex = output_path.parent_path() / folder_name /
                                       (folder_name.string() + "_diffuse.png");
  ASSERT_TRUE(std::filesystem::exists(expected_tex))
      << "Expected " << expected_tex << " to exist";

  // 5. Load and Verify Pixels
  int w, h, c;
  unsigned char* data = stbi_load(expected_tex.string().c_str(), &w, &h, &c, 4);
  ASSERT_TRUE(data != nullptr);
  ASSERT_EQ(w, 2);
  ASSERT_EQ(h, 2);
  ASSERT_EQ(c, 4);  // We requested 4 channels

  // Pixel 0 (Black) -> Alpha should be 0
  EXPECT_EQ(data[3], 0);

  // Pixel 1 (Red) -> Alpha should be 255
  EXPECT_EQ(data[4 + 3], 255);

  // Pixel 2 (Green) -> Alpha 255
  EXPECT_EQ(data[8 + 3], 255);

  // Pixel 3 (White) -> Alpha 255
  EXPECT_EQ(data[12 + 3], 255);

  stbi_image_free(data);
  std::filesystem::remove_all(temp_dir);
}

}  // namespace ioq3_map
