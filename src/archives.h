#ifndef IOQ3_MAP_ARCHIVES_H_
#define IOQ3_MAP_ARCHIVES_H_

#include <filesystem>
#include <optional>
#include <vector>

namespace ioq3_map {

std::vector<std::filesystem::path> ListArchives(
    const std::filesystem::path& base_path);

struct VirtualFilesystem {
  std::filesystem::path mount_point;

  VirtualFilesystem(std::filesystem::path mount);
  ~VirtualFilesystem();

  // Delete copy, allow move
  VirtualFilesystem(const VirtualFilesystem&) = delete;
  VirtualFilesystem& operator=(const VirtualFilesystem&) = delete;
  VirtualFilesystem(VirtualFilesystem&&) = default;
  VirtualFilesystem& operator=(VirtualFilesystem&&) = default;
};

std::optional<VirtualFilesystem> BuildVirtualFilesystem(
    const std::vector<std::filesystem::path>& archives);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_ARCHIVES_H_
