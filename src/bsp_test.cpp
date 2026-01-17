#include "bsp.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace ioq3_map {
namespace {

namespace fs = std::filesystem;

class BspTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = fs::current_path() / "test_bsp_data";
    fs::create_directories(test_dir_);
  }

  void TearDown() override { fs::remove_all(test_dir_); }

  // Helper to write a BSP file
  void WriteBsp(const fs::path& path, int ident, int version,
                const std::vector<char>& extra_data = {}) {
    std::ofstream ofs(path, std::ios::binary);

    // Write Header
    ofs.write(reinterpret_cast<const char*>(&ident), sizeof(ident));
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // 17 Lumps (offset, len)
    // We can just set them to 0,0 for basic header check, or point to
    // extra_data
    int offset = sizeof(int) * 2 + sizeof(int) * 2 * 17;  // Header size

    for (int i = 0; i < 17; ++i) {
      // If we want to test load, we might want valid lumps.
      // For now, valid header check lumps can be empty.
      int lump_offset = offset;
      int lump_len = 0;
      if (i == 0 && !extra_data.empty()) {
        lump_len = extra_data.size();
      }

      ofs.write(reinterpret_cast<const char*>(&lump_offset), sizeof(int));
      ofs.write(reinterpret_cast<const char*>(&lump_len), sizeof(int));
    }

    if (!extra_data.empty()) {
      ofs.write(extra_data.data(), extra_data.size());
    }
  }

  fs::path test_dir_;
};

TEST_F(BspTest, IsValidBspRejectsNonExistent) {
  EXPECT_FALSE(IsValidBsp(test_dir_ / "does_not_exist.bsp"));
}

TEST_F(BspTest, IsValidBspRejectsTooSmall) {
  fs::path p = test_dir_ / "toosmall.bsp";
  std::ofstream(p).put('a');
  EXPECT_FALSE(IsValidBsp(p));
}

TEST_F(BspTest, IsValidBspRejectsBadMagic) {
  fs::path p = test_dir_ / "badmagic.bsp";
  WriteBsp(p, 0x12345678, 0x2E);
  EXPECT_FALSE(IsValidBsp(p));
}

TEST_F(BspTest, IsValidBspRejectsBadVersion) {
  fs::path p = test_dir_ / "badver.bsp";
  WriteBsp(p, 0x50534249, 0x2F);
  EXPECT_FALSE(IsValidBsp(p));
}

TEST_F(BspTest, IsValidBspAcceptsValid) {
  fs::path p = test_dir_ / "valid.bsp";
  WriteBsp(p, 0x50534249, 0x2E);
  EXPECT_TRUE(IsValidBsp(p));
}

TEST_F(BspTest, LoadBspReadsLumps) {
  fs::path p = test_dir_ / "load.bsp";
  std::vector<char> data = {'H', 'e', 'l', 'l', 'o'};
  WriteBsp(p, 0x50534249, 0x2E, data);

  auto bsp = LoadBsp(p);
  ASSERT_TRUE(bsp.has_value());

  // Check Lump 0 (Entities) which we populated
  auto it = bsp->lumps.find(LumpType::Entities);
  ASSERT_NE(it, bsp->lumps.end());

  std::string_view content = it->second;
  EXPECT_EQ(content, "Hello");
}

}  // namespace
}  // namespace ioq3_map
