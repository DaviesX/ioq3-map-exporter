#include <gflags/gflags.h>
#include <glog/logging.h>

#include <filesystem>
#include <iostream>

#include "merger.h"

// Merges an artist-edited map export back into the original exported map:
//   1. Replaces the placeholder albedo + ORM + normal PBR textures with the
//      artist's.
//   2. Replaces the punctual light sources in the export glTF with the artist's.
DEFINE_string(artist_base, "", "Source dir: the artist-edited export");
DEFINE_string(export_base, "", "Destination dir: the original export to update");

DEFINE_bool(port_textures, true,
            "Replace placeholder albedo + ORM + normal textures");
DEFINE_bool(port_lights, true, "Replace punctual light sources in the glTF");

DEFINE_string(artist_gltf, "scene.gltf",
              "glTF filename (in --artist_base) to read lights from");
DEFINE_string(export_gltf, "scene.gltf",
              "glTF filename (in --export_base) to update in place");

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  FLAGS_logtostderr = 1;
  google::InitGoogleLogging(argv[0]);

  if (FLAGS_artist_base.empty() || FLAGS_export_base.empty()) {
    std::cerr << "Usage: --artist_base=<src-dir> --export_base=<dest-dir>"
              << " [--noport_textures] [--noport_lights]"
              << " [--artist_gltf=scene.gltf] [--export_gltf=scene.gltf]"
              << std::endl;
    return 1;
  }

  ioq3_map::PortConfig config{
      .port_textures = FLAGS_port_textures,
      .port_lights = FLAGS_port_lights,
      .artist_gltf = FLAGS_artist_gltf,
      .export_gltf = FLAGS_export_gltf,
  };

  LOG(INFO) << "Porting artist edit: " << FLAGS_artist_base << " -> "
            << FLAGS_export_base;

  ioq3_map::PortStats stats;
  if (!ioq3_map::RunPort(FLAGS_artist_base, FLAGS_export_base, config,
                         &stats)) {
    LOG(ERROR) << "Port failed.";
    return 1;
  }

  LOG(INFO) << "Textures replaced: " << stats.textures_replaced
            << " (missing dest: " << stats.textures_missing_dest << ")";
  LOG(INFO) << "Lights updated: " << stats.lights_updated
            << ", added: " << stats.lights_added
            << ", removed: " << stats.lights_removed;
  LOG(INFO) << "Port complete.";
  return 0;
}
