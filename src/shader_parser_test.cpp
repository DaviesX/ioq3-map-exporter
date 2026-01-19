#include "shader_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "archives.h"

namespace ioq3_map {
namespace {

using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

class ShaderParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temporary directory for VFS
    temp_dir_ = std::filesystem::temp_directory_path() / "shader_parser_test";
    std::filesystem::create_directories(temp_dir_);

    // Create 'scripts' directory
    scripts_dir_ = temp_dir_ / "scripts";
    std::filesystem::create_directories(scripts_dir_);

    // Setup VFS
    vfs_.emplace(temp_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  void CreateShaderFile(const std::string& filename,
                        const std::string& content) {
    std::ofstream file(scripts_dir_ / filename);
    file << content;
  }

  void CreateFile(const std::string& relative_path,
                  const std::string& content) {
    std::filesystem::path full_path = temp_dir_ / relative_path;
    std::filesystem::create_directories(full_path.parent_path());
    std::ofstream file(full_path);
    file << content;
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path scripts_dir_;
  std::optional<VirtualFilesystem> vfs_;
};

TEST_F(ShaderParserTest, ListQ3ShaderFindsFiles) {
  CreateShaderFile("test1.shader", "");
  CreateShaderFile("test2.shader", "");
  CreateShaderFile("ignore.txt", "");

  auto files = ListQ3ShaderScripts(*vfs_);
  EXPECT_THAT(files, UnorderedElementsAre(scripts_dir_ / "test1.shader",
                                          scripts_dir_ / "test2.shader"));
}

TEST_F(ShaderParserTest, ParseShaderSimple) {
  CreateFile("textures/common/base.tga", "");
  CreateShaderFile("simple.shader", R"(
textures/common/simple
{
    q3map_surfacelight 100
    q3map_lightimage textures/common/glow.tga
    surfaceparm simple_flag
    {
        map textures/common/base.tga
    }
}
)");

  auto files = ListQ3ShaderScripts(*vfs_);
  auto shaders = ParseShaderScripts(*vfs_, files);

  ASSERT_EQ(shaders.size(), 1);
  const auto& shader = shaders["textures/common/simple"];
  EXPECT_EQ(shader.name, "textures/common/simple");
  EXPECT_FLOAT_EQ(shader.q3map_surfacelight, 100.0f);
  EXPECT_EQ(shader.q3map_lightimage, "textures/common/glow.tga");
  EXPECT_THAT(shader.texture_layers,
              ElementsAre(Q3TextureLayer{
                  .path = "/tmp/shader_parser_test/textures/common/base.tga"}));
}

TEST_F(ShaderParserTest, ParseShaderSun) {
  CreateShaderFile("sun.shader", R"(
textures/skies/sun_sky
{
    q3map_sun 1.0 0.9 0.8 200 45 60
    surfaceparm sky
}
)");

  auto files = ListQ3ShaderScripts(*vfs_);
  auto shaders = ParseShaderScripts(*vfs_, files);

  ASSERT_EQ(shaders.size(), 1);
  const auto& shader = shaders["textures/skies/sun_sky"];

  EXPECT_FLOAT_EQ(shader.q3map_sun_color.x(), 1.0f);
  EXPECT_FLOAT_EQ(shader.q3map_sun_color.y(), 0.9f);
  EXPECT_FLOAT_EQ(shader.q3map_sun_color.z(), 0.8f);
  EXPECT_FLOAT_EQ(shader.q3map_sun_intensity, 200.0f);
  EXPECT_FLOAT_EQ(shader.q3map_sun_direction.x(), 45.0f);
  EXPECT_FLOAT_EQ(shader.q3map_sun_direction.y(), 60.0f);
}

TEST_F(ShaderParserTest, ParseMultipleShaders) {
  CreateShaderFile("multi.shader", R"(
textures/s1
{
    q3map_surfacelight 50
}
textures/s2
{
    q3map_surfacelight 150
}
)");

  auto files = ListQ3ShaderScripts(*vfs_);
  auto shaders = ParseShaderScripts(*vfs_, files);

  ASSERT_EQ(shaders.size(), 2);
  EXPECT_FLOAT_EQ(shaders["textures/s1"].q3map_surfacelight, 50.0f);
  EXPECT_FLOAT_EQ(shaders["textures/s2"].q3map_surfacelight, 150.0f);
}

}  // namespace
}  // namespace ioq3_map
