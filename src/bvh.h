#ifndef IOQ3_MAP_EXPORTER_BVH_H_
#define IOQ3_MAP_EXPORTER_BVH_H_

#include <embree4/rtcore.h>

#include <Eigen/Dense>  // IWYU pragma: keep
#include <vector>

#include "scene.h"

namespace ioq3_map {

// Result of a ray cast: distance to the nearest hit (+infinity on a miss) and
// the hit triangle's geometric normal (Embree Ng, unnormalized; its sign
// follows the triangle winding, so for the exporter's outward-wound front
// surfaces it points out of the solid). The normal is meaningless on a miss.
struct RayHit {
  float distance;
  Eigen::Vector3f normal;
};

// A read-only Embree BVH over a set of triangle Geometries, used by the
// solidifier to answer "how far is the nearest surface behind this wall?"
// (backward clearance) so the extruded shell can be clamped to available space.
//
// Vertices are baked into world space (each Geometry's `transform` applied) at
// construction, so queries take world-space origins/directions. This is the
// exact same Embree machinery the sh-baker path tracer uses; here it is brought
// into the exporter so the extrusion is spatially aware.
class SceneBVH {
 public:
  explicit SceneBVH(const std::vector<const Geometry*>& geometries);
  ~SceneBVH();

  SceneBVH(const SceneBVH&) = delete;
  SceneBVH& operator=(const SceneBVH&) = delete;

  // True once the device/scene were created successfully.
  bool valid() const { return scene_ != nullptr; }

  // Nearest hit from `origin` along `dir` (need not be unit): distance and the
  // hit triangle's geometric normal, ignoring hits nearer than `tnear` (world
  // units; used to skip the surface the ray starts on). Distance is +infinity
  // when nothing is hit.
  RayHit Cast(const Eigen::Vector3f& origin, const Eigen::Vector3f& dir,
              float tnear = 1e-3f) const;

  // Distance-only convenience wrapper over Cast().
  float NearestHit(const Eigen::Vector3f& origin, const Eigen::Vector3f& dir,
                   float tnear = 1e-3f) const {
    return Cast(origin, dir, tnear).distance;
  }

 private:
  RTCDevice device_ = nullptr;
  RTCScene scene_ = nullptr;
};

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_BVH_H_
