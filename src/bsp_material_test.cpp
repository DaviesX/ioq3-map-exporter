#include "bsp_material.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "bsp.h"
#include "shader_parser.h"  // For Q3Shader

namespace ioq3_map {
namespace {

class BspMaterialTest : public ::testing::Test {
 protected:
  std::filesystem::path temp_dir;

  void SetUp() override {
    // Create a unique temp directory for the VFS
    temp_dir = std::filesystem::temp_directory_path() / "ioq3_map_test_vfs";
    if (std::filesystem::exists(temp_dir)) {
      std::filesystem::remove_all(temp_dir);
    }
    std::filesystem::create_directories(temp_dir);
  }

  void TearDown() override {
    if (std::filesystem::exists(temp_dir)) {
      std::filesystem::remove_all(temp_dir);
    }
  }

  void SetShaderLump(BSP& bsp, const std::vector<dshader_t>& shaders) {
    size_t size = shaders.size() * sizeof(dshader_t);
    // Ensure buffer has space and append.
    // Note: Reallocation invalidates existing views,
    // so in a real scenario we'd reserve or be careful.
    // Here we assume we only set one lump for the test.
    size_t offset = bsp.buffer.size();
    bsp.buffer.resize(offset + size);
    std::memcpy(bsp.buffer.data() + offset, shaders.data(), size);

    // LumpType::Textures corresponds to Lump 1 (Shaders) in Q3
    bsp.lumps[LumpType::Textures] =
        std::string_view(bsp.buffer.data() + offset, size);
  }
};

TEST_F(BspMaterialTest, BuildBSPMaterialsMatchesParsed) {
  BSP bsp;
  dshader_t ds1;
  std::memset(&ds1, 0, sizeof(ds1));
  std::strcpy(ds1.shader, "textures/common/test_shader");
  ds1.surface_flags = 123;
  ds1.content_flags = 456;

  SetShaderLump(bsp, {ds1});

  std::unordered_map<Q3ShaderName, Q3Shader> parsed;
  Q3Shader q3s;
  q3s.name = "textures/common/test_shader";
  q3s.q3map_surfacelight = 100.0f;
  parsed[q3s.name] = q3s;

  VirtualFilesystem vfs("dummy_mount");
  auto materials =
      BuildBSPMaterials(bsp, parsed, [&vfs](const std::string& shader_name) {
        return CreateDefaultShader(shader_name, vfs);
      });

  ASSERT_EQ(materials.size(), 1);
  const auto& mat = materials[0];
  EXPECT_EQ(mat.name, "textures/common/test_shader");
  EXPECT_EQ(mat.surface_flags, 123);                // From Lump
  EXPECT_EQ(mat.content_flags, 456);                // From Lump
  EXPECT_FLOAT_EQ(mat.q3map_surfacelight, 100.0f);  // From Parsed
}

TEST_F(BspMaterialTest, BuildBSPMaterialsDefaultWhenMissing) {
  BSP bsp;
  dshader_t ds1;
  std::memset(&ds1, 0, sizeof(ds1));
  std::strcpy(ds1.shader, "textures/common/unknown");
  ds1.surface_flags = 1;
  SetShaderLump(bsp, {ds1});

  std::unordered_map<Q3ShaderName, Q3Shader> parsed;  // Empty

  // Point VFS to our temp dir, which is empty.
  VirtualFilesystem vfs(temp_dir);

  auto materials =
      BuildBSPMaterials(bsp, parsed, [&vfs](const std::string& shader_name) {
        return CreateDefaultShader(shader_name, vfs);
      });

  // Since "textures/common/unknown" + extensions does not exist in temp_dir,
  // it should be skipped.
  ASSERT_EQ(materials.size(), 0);
}

TEST_F(BspMaterialTest, BuildBSPMaterialsFindsTextureOnDisk) {
  BSP bsp;
  dshader_t ds1;
  std::memset(&ds1, 0, sizeof(ds1));
  std::strcpy(ds1.shader, "textures/common/concrete");
  ds1.surface_flags = 1;
  SetShaderLump(bsp, {ds1});

  std::unordered_map<Q3ShaderName, Q3Shader> parsed;  // Empty

  // Create the texture file in the VFS
  std::filesystem::path texture_dir = temp_dir / "textures/common";
  std::filesystem::create_directories(texture_dir);
  // Create .jpg
  std::ofstream(texture_dir / "concrete.jpg").put('\0');

  VirtualFilesystem vfs(temp_dir);
  auto materials =
      BuildBSPMaterials(bsp, parsed, [&vfs](const std::string& shader_name) {
        return CreateDefaultShader(shader_name, vfs);
      });

  ASSERT_EQ(materials.size(), 1);
  const auto& mat = materials[0];
  EXPECT_EQ(mat.name, "textures/common/concrete");
  // Default shader parsed from disk should have the texture layer
  ASSERT_EQ(mat.texture_layers.size(), 1);
  EXPECT_EQ(mat.texture_layers[0].path, "textures/common/concrete.jpg");
  EXPECT_EQ(mat.surface_flags, 1);
}

}  // namespace
}  // namespace ioq3_map
