#include "archives.h"

#include <glog/logging.h>
#include <minizip/unzip.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

namespace ioq3_map {

std::vector<std::filesystem::path> ListArchives(
    const std::filesystem::path& base_path) {
  std::vector<std::filesystem::path> archives;
  if (!std::filesystem::exists(base_path) ||
      !std::filesystem::is_directory(base_path)) {
    LOG(ERROR) << "Base path does not exist or is not a directory: "
               << base_path;
    return archives;
  }

  for (const auto& entry : std::filesystem::directory_iterator(base_path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".pk3") {
      archives.push_back(entry.path());
    }
  }

  // Sort alphabetically to ensure correct load order (Q3 standard)
  std::sort(archives.begin(), archives.end());
  return archives;
}

VirtualFilesystem::VirtualFilesystem(std::filesystem::path mount)
    : mount_point(std::move(mount)) {}

VirtualFilesystem::~VirtualFilesystem() {
  if (!mount_point.empty() && std::filesystem::exists(mount_point)) {
    if (mount_point.filename() == "vfs_mount_point") {
      LOG(INFO) << "Cleaning up virtual filesystem at: " << mount_point;
      std::filesystem::remove_all(mount_point);
    } else {
      LOG(ERROR)
          << "Safety check failed: Refusing to delete mount point that is "
             "not named 'vfs_mount_point': "
          << mount_point;
    }
  }
}

// Helper to extract a single file from zip
bool ExtractCurrentFile(unzFile zf, const std::filesystem::path& dest_path) {
  char filename_inzip[256];
  unz_file_info64 file_info;

  if (unzGetCurrentFileInfo64(zf, &file_info, filename_inzip,
                              sizeof(filename_inzip), NULL, 0, NULL,
                              0) != UNZ_OK) {
    LOG(ERROR) << "Could not get file info";
    return false;
  }

  // Check if it is a directory (ends with /)
  std::string filename(filename_inzip);
  bool is_dir = (!filename.empty() &&
                 (filename.back() == '/' || filename.back() == '\\'));

  std::filesystem::path out_file = dest_path / filename;

  if (is_dir) {
    std::filesystem::create_directories(out_file);
    return true;
  }

  // Ensure parent directory exists
  std::filesystem::create_directories(out_file.parent_path());

  if (unzOpenCurrentFile(zf) != UNZ_OK) {
    LOG(ERROR) << "Could not open file in zip: " << filename;
    return false;
  }

  // If file exists, do not overwrite (as per specs: "When colliding file names,
  // do not overwrite. Skip the file.") Wait, the specification says: "When
  // colliding file names, do not overwrite. Skip the file." But usually in Q3,
  // later PK3s overwrite earlier ones? Let's re-read the specs in GEMINI.md or
  // USER RULES. "Write a function to unzip all pk3 files into a temporary
  // directory. When colliding file names, do not overwrite. Skip the file.
  // Return the set of files." AND "List all the pk3 files in the base path in
  // alphabetical order." So if pak0.pk3 has "A", and pak1.pk3 has "A", we unzip
  // pak0 first, write "A". Then unzipping pak1, we find "A" exists, so we SKIP.
  // This effectively means "First loaded wins"?
  // Quake 3 logic is usually "Last loaded wins" (overwrite).
  // Let's check the Phase 1 instructions again in GEMINI.md.
  // "When colliding file names, do not overwrite. Skip the file."
  // This implies First Wins if we process in order.
  // Maybe the user wants "First wins"? Or maybe they implied "Do not overwrite"
  // means "If I already extracted it from a previous (lower priority) archive,
  // keep it"? But Q3 loads pak0, then pak1, pak1 overwrites pak0. So if we
  // iterate ListArchives (pak0, pak1...), and we want pak1 to overwrite pak0,
  // we SHOULD overwrite. OR, maybe we iterate in REVERSE order and then "Do not
  // overwrite" makes sense (Last packed wins = First extracted wins). The
  // instructions say: "List all the pk3 files in the base path in alphabetical
  // order." (pak0, pak1...) Then "Unzip all pk3 files... When colliding... do
  // not overwrite". If I do that: pak0 writes "texture.jpg". pak1 has
  // "texture.jpg" but I skip it. Result: "texture.jpg" is from pak0. Real Q3:
  // pak1 overwrites pak0. "texture.jpg" is from pak1. So "Do not overwrite" +
  // "Alphabetical Order" = "Inverse Q3 Priority". This seems wrong for a map
  // exporter. HOWEVER, I must follow the user's explicit instruction: "When
  // colliding file names, do not overwrite." Unless... I should reverse the
  // list? "Implement a function to list all the pk3 files in the base path in
  // alphabetical order." "Write a function to unzip all pk3 files..." If I
  // follow literally, I get inverse priority. USE JUDGEMENT. The user likely
  // wants correct Q3 behavior. Correct Q3 behavior is: later archives override
  // earlier ones. So if I have pak0...pak8. pak8 is highest priority. If I
  // iterate 0..8 and "no overwrite", I get pak0 data. If I iterate 8..0 and "no
  // overwrite", I get pak8 data. The instruction "List... in alphabetical
  // order" implies the return of ListPk3Files is A->Z. The instruction "Unzip
  // all pk3 files..." implies using that list. Maybe I should iterate in
  // reverse? Or maybe the user *wants* "Do not overwrite" means "Overwrite if
  // newer"? No "Do not overwrite" means don't. I will iterate in REVERSE order
  // so that high priority files (z_pak) are extracted first, and then lower
  // priority ones don't overwrite them. This satisfies "Do not overwrite" and
  // "Correct Q3 Priority".

  if (std::filesystem::exists(out_file)) {
    unzCloseCurrentFile(zf);
    return true;  // Skip
  }

  FILE* fout = fopen(out_file.string().c_str(), "wb");
  if (!fout) {
    LOG(ERROR) << "Could not open output file: " << out_file;
    unzCloseCurrentFile(zf);
    return false;
  }

  std::vector<char> buffer(4096);
  int read_bytes;
  while ((read_bytes = unzReadCurrentFile(zf, buffer.data(), buffer.size())) >
         0) {
    fwrite(buffer.data(), 1, read_bytes, fout);
  }

  fclose(fout);
  unzCloseCurrentFile(zf);
  return true;
}

std::optional<VirtualFilesystem> BuildVirtualFilesystem(
    const std::vector<std::filesystem::path>& archives) {
  if (archives.empty()) {
    return std::nullopt;
  }

  // Create mount point
  std::filesystem::path mount_point =
      std::filesystem::current_path() / "vfs_mount_point";
  if (std::filesystem::exists(mount_point)) {
    std::filesystem::remove_all(mount_point);
  }
  std::filesystem::create_directories(mount_point);

  // Iterate in REVERSE order to respect Q3 priority with "no overwrite" logic
  // Q3: pak0 < pak1 < ... < z_pak
  // We want z_pak content to win.
  // So we extract z_pak first. Then pak[n-1]... if file exists (from z_pak), we
  // skip. This results in z_pak version being kept.
  for (auto it = archives.rbegin(); it != archives.rend(); ++it) {
    unzFile zf = unzOpen64(it->string().c_str());
    if (!zf) {
      LOG(WARNING) << "Failed to open archive: " << *it;
      continue;
    }

    if (unzGoToFirstFile(zf) == UNZ_OK) {
      do {
        ExtractCurrentFile(zf, mount_point);
      } while (unzGoToNextFile(zf) == UNZ_OK);
    }

    unzClose(zf);
  }

  return VirtualFilesystem(mount_point);
}

}  // namespace ioq3_map
