#include "archives.h"

#include <gtest/gtest.h>
#include <minizip/zip.h>
#include <stddef.h>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace ioq3_map {
namespace {

namespace fs = std::filesystem;

class ArchivesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = fs::current_path() / "test_data";
    fs::create_directories(test_dir_);
  }

  void TearDown() override { fs::remove_all(test_dir_); }

  void CreateDummyZip(const fs::path& path, const std::string& filename,
                      const std::string& content) {
    zipFile zf = zipOpen64(path.u8string().c_str(), 0);
    ASSERT_TRUE(zf != nullptr);

    zip_fileinfo zi = {};
    int err = zipOpenNewFileInZip(zf, filename.c_str(), &zi, NULL, 0, NULL, 0,
                                  NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    ASSERT_EQ(err, ZIP_OK);

    err = zipWriteInFileInZip(zf, content.data(), content.size());
    ASSERT_EQ(err, ZIP_OK);

    err = zipCloseFileInZip(zf);
    ASSERT_EQ(err, ZIP_OK);

    err = zipClose(zf, NULL);
    ASSERT_EQ(err, ZIP_OK);
  }

  fs::path test_dir_;
};

TEST_F(ArchivesTest, ListArchivesReturnsSortedFiles) {
  // Create dummy pk3 files (mixed order)
  std::ofstream(test_dir_ / "z_pak.pk3").close();
  std::ofstream(test_dir_ / "pak0.pk3").close();
  std::ofstream(test_dir_ / "pak1.pk3").close();
  std::ofstream(test_dir_ / "ignore_me.txt").close();

  auto archives = ListArchives(test_dir_);

  ASSERT_EQ(archives.size(), 3);
  EXPECT_EQ(archives[0].filename(), "pak0.pk3");
  EXPECT_EQ(archives[1].filename(), "pak1.pk3");
  EXPECT_EQ(archives[2].filename(), "z_pak.pk3");
}

TEST_F(ArchivesTest, BuildVirtualFilesystemExtractsFiles) {
  // Create pak0.pk3 -> file1.txt: "from pak0"
  // Create pak1.pk3 -> file1.txt: "from pak1" (should overwrite pak0 logicaly,
  // but our implementation logic should handle this) Create pak1.pk3 ->
  // file2.txt: "from pak1"

  // NOTE: Our implementation iterates in reverse order and skips if exists.
  // So pak1 (processed first) writes file1.txt ("from pak1").
  // Then pak0 (processed second) sees file1.txt exists and skips.
  // Result: file1.txt is "from pak1". This is correct Q3 behavior (later pak
  // overwrites).

  CreateDummyZip(test_dir_ / "pak0.pk3", "file1.txt", "from pak0");
  CreateDummyZip(test_dir_ / "pak1.pk3", "file1.txt", "from pak1");
  // Append another file to pak1
  {
    // minizip C API for appending is annoying, let's just create a separate one
    // or assume one file per zip for simplicity of test helper or just accept
    // CreateDummyZip overwrites. Let's make CreateDummyZip support appending?
    // zipOpen64 with APPEND_STATUS_ADDINZIP
  }
  // Simpler: Just test overwrite logic.

  std::vector<fs::path> archives = {test_dir_ / "pak0.pk3",
                                    test_dir_ / "pak1.pk3"};
  auto vfs = BuildVirtualFilesystem(archives);

  ASSERT_TRUE(vfs.has_value());
  EXPECT_TRUE(fs::exists(vfs->mount_point));

  fs::path file1 = vfs->mount_point / "file1.txt";
  ASSERT_TRUE(fs::exists(file1));

  std::ifstream ifs(file1);
  std::string content((std::istreambuf_iterator<char>(ifs)),
                      (std::istreambuf_iterator<char>()));
  EXPECT_EQ(content, "from pak1");
}

TEST_F(ArchivesTest, UseRealData) {
  // This test assumes it runs from the repo root and "data" folder exists with
  // pak0.pk3.
  fs::path data_path = "data";
  if (!fs::exists(data_path)) {
    // If running from build dir, maybe it's ../data ?
    // But user said: "assume the working directory ... is always at the root of
    // this repo." So we expect "data" to be there. If not, we skip or fail?
    // Let's Assert.
    FAIL() << "data directory not found at " << fs::absolute(data_path);
  }

  auto archives = ListArchives(data_path);
  ASSERT_FALSE(archives.empty()) << "No archives found in data/";

  bool found_pak0 = false;
  for (const auto& arch : archives) {
    if (arch.filename() == "pak0.pk3") {
      found_pak0 = true;
      break;
    }
  }
  EXPECT_TRUE(found_pak0) << "pak0.pk3 not found in data/";

  auto vfs = BuildVirtualFilesystem(archives);
  ASSERT_TRUE(vfs.has_value());
  EXPECT_TRUE(fs::exists(vfs->mount_point));
}

}  // namespace
}  // namespace ioq3_map
