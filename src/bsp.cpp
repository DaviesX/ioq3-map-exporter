#include "bsp.h"

#include <glog/logging.h>

#include <fstream>
#include <iostream>

namespace ioq3_map {

namespace {

struct dheader_t {
  int ident;    // IBSP
  int version;  // 0x2E
  struct {
    int fileofs;
    int filelen;
  } lumps[17];
};

constexpr int kIBSP = 0x50534249;  // "IBSP" little endian
constexpr int kVersion = 0x2E;

}  // namespace

bool IsValidBsp(const std::filesystem::path& bsp_file_path) {
  if (!std::filesystem::exists(bsp_file_path)) {
    LOG(ERROR) << "BSP file does not exist: " << bsp_file_path;
    return false;
  }

  std::ifstream file(bsp_file_path, std::ios::binary);
  if (!file) {
    LOG(ERROR) << "Could not open BSP file: " << bsp_file_path;
    return false;
  }

  dheader_t header;
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (file.gcount() != sizeof(header)) {
    LOG(ERROR) << "BSP file parsing failed: file too small for header.";
    return false;
  }

  if (header.ident != kIBSP) {
    // Try checking if big endian or inverted?
    // Q3 BSPs are typically little endian.
    LOG(ERROR) << "Invalid BSP magic: " << std::hex << header.ident
               << " expected " << kIBSP;
    return false;
  }

  if (header.version != kVersion) {
    LOG(ERROR) << "Invalid BSP version: " << header.version << " expected "
               << kVersion;
    return false;
  }

  return true;
}

std::optional<BSP> LoadBsp(const std::filesystem::path& bsp_file_path) {
  if (!IsValidBsp(bsp_file_path)) {
    return std::nullopt;
  }

  std::ifstream file(bsp_file_path, std::ios::binary | std::ios::ate);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  BSP bsp;
  bsp.buffer.resize(size);
  if (!file.read(bsp.buffer.data(), size)) {
    LOG(ERROR) << "Failed to read BSP file content.";
    return std::nullopt;
  }

  const dheader_t* header =
      reinterpret_cast<const dheader_t*>(bsp.buffer.data());

  // Map Lumps
  for (int i = 0; i < 17; ++i) {
    int offset = header->lumps[i].fileofs;
    int length = header->lumps[i].filelen;

    if (offset + length > size) {
      LOG(ERROR) << "Lump " << i << " out of bounds.";
      return std::nullopt;
    }

    bsp.lumps[static_cast<LumpType>(i)] =
        std::string_view(bsp.buffer.data() + offset, length);
  }

  return bsp;
}

}  // namespace ioq3_map
