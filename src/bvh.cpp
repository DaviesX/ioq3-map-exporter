#include "bvh.h"

#include <glog/logging.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace ioq3_map {

SceneBVH::SceneBVH(const std::vector<const Geometry*>& geometries) {
  device_ = rtcNewDevice(nullptr);
  if (device_ == nullptr) {
    LOG(ERROR) << "Failed to create Embree device; solidification will fall "
                  "back to fixed thickness.";
    return;
  }

  scene_ = rtcNewScene(device_);
  rtcSetSceneBuildQuality(scene_, RTC_BUILD_QUALITY_HIGH);

  for (const Geometry* g : geometries) {
    if (g == nullptr || g->vertices.empty() || g->indices.size() < 3) {
      continue;
    }

    RTCGeometry rtc_geo = rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_TRIANGLE);

    // Vertices, baked to world space. Embree owns the buffer, so no lifetime
    // dependency on the source Geometry.
    const size_t vertex_count = g->vertices.size();
    float* vertex_buffer = static_cast<float*>(rtcSetNewGeometryBuffer(
        rtc_geo, RTC_BUFFER_TYPE_VERTEX, /*slot=*/0, RTC_FORMAT_FLOAT3,
        /*byteStride=*/3 * sizeof(float), /*itemCount=*/vertex_count));
    for (size_t i = 0; i < vertex_count; ++i) {
      const Eigen::Vector3f w = g->transform * g->vertices[i];
      vertex_buffer[3 * i + 0] = w.x();
      vertex_buffer[3 * i + 1] = w.y();
      vertex_buffer[3 * i + 2] = w.z();
    }

    // Triangle indices (truncate any trailing partial triangle).
    const size_t triangle_count = g->indices.size() / 3;
    uint32_t* index_buffer = static_cast<uint32_t*>(rtcSetNewGeometryBuffer(
        rtc_geo, RTC_BUFFER_TYPE_INDEX, /*slot=*/0, RTC_FORMAT_UINT3,
        /*byteStride=*/3 * sizeof(uint32_t), /*itemCount=*/triangle_count));
    std::memcpy(index_buffer, g->indices.data(),
                triangle_count * 3 * sizeof(uint32_t));

    rtcCommitGeometry(rtc_geo);
    rtcAttachGeometry(scene_, rtc_geo);
    rtcReleaseGeometry(rtc_geo);
  }

  rtcCommitScene(scene_);
}

SceneBVH::~SceneBVH() {
  if (scene_ != nullptr) rtcReleaseScene(scene_);
  if (device_ != nullptr) rtcReleaseDevice(device_);
}

float SceneBVH::NearestHit(const Eigen::Vector3f& origin,
                           const Eigen::Vector3f& dir, float tnear) const {
  constexpr float kInf = std::numeric_limits<float>::infinity();
  if (scene_ == nullptr) return kInf;

  alignas(16) RTCRayHit rh;
  rh.ray.org_x = origin.x();
  rh.ray.org_y = origin.y();
  rh.ray.org_z = origin.z();
  rh.ray.dir_x = dir.x();
  rh.ray.dir_y = dir.y();
  rh.ray.dir_z = dir.z();
  rh.ray.tnear = tnear;
  rh.ray.tfar = kInf;
  rh.ray.time = 0.0f;
  rh.ray.mask = -1;
  rh.ray.id = 0;
  rh.ray.flags = 0;
  rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
  rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

  RTCIntersectArguments args;
  rtcInitIntersectArguments(&args);
  rtcIntersect1(scene_, &rh, &args);

  if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return kInf;

  // Embree's tfar is parametric in |dir|; convert to a world-space distance.
  return rh.ray.tfar * dir.norm();
}

}  // namespace ioq3_map
