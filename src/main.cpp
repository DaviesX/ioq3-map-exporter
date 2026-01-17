#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iostream>

#include "archives.h"
#include "bsp.h"

DEFINE_string(base_path, "", "Path to Quake 3 .pk3 archives");
DEFINE_string(map, "", "Map name (e.g., q3dm1)");
DEFINE_string(output, "", "Output directory");

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  FLAGS_logtostderr = 1;
  google::InitGoogleLogging(argv[0]);

  if (FLAGS_base_path.empty() || FLAGS_map.empty() || FLAGS_output.empty()) {
    std::cerr << "Usage: --base-path=<path> --map=<mapname> --output=<dir>"
              << std::endl;
    return 1;
  }

  LOG(INFO) << "Starting ioq3-map-exporter";
  LOG(INFO) << "Base Path: " << FLAGS_base_path;
  LOG(INFO) << "Map: " << FLAGS_map;
  LOG(INFO) << "Output: " << FLAGS_output;

  // 1. List Archives
  auto archives = ioq3_map::ListArchives(FLAGS_base_path);
  if (archives.empty()) {
    LOG(ERROR) << "No PK3 archives found in " << FLAGS_base_path;
    return 1;
  }
  LOG(INFO) << "Found " << archives.size() << " archives.";

  // 2. Build Virtual Filesystem
  auto vfs = ioq3_map::BuildVirtualFilesystem(archives);
  if (!vfs) {
    LOG(ERROR) << "Failed to build virtual filesystem.";
    return 1;
  }
  LOG(INFO) << "Mounted VFS at: " << vfs->mount_point;

  // 3. Locate Map
  // assumption: --map "q3dm1" implies "maps/q3dm1.bsp"
  std::string map_name = FLAGS_map;
  if (map_name.find(".bsp") == std::string::npos) {
    map_name += ".bsp";
  }

  // Check if user provided "maps/" prefix
  std::filesystem::path map_path;
  if (map_name.find("maps/") == 0 || map_name.find("maps\\") == 0) {
    map_path = vfs->mount_point / map_name;
  } else {
    map_path = vfs->mount_point / "maps" / map_name;
  }

  if (!std::filesystem::exists(map_path)) {
    LOG(ERROR) << "Map file not found in VFS: " << map_path;
    return 1;
  }
  LOG(INFO) << "Found map at: " << map_path;

  // 4. Load BSP
  auto bsp = ioq3_map::LoadBsp(map_path);
  if (!bsp) {
    LOG(ERROR) << "Failed to load BSP file.";
    return 1;
  }
  LOG(INFO) << "Successfully loaded BSP header. Lumps found: "
            << bsp->lumps.size();

  LOG(INFO) << "Phase 1 Complete. Exiting.";
  return 0;
}
