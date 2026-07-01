#ifndef IOQ3_MAP_EXPORTER_EXTRUDE_H_
#define IOQ3_MAP_EXPORTER_EXTRUDE_H_

#include <Eigen/Dense>  // IWYU pragma: keep
#include <functional>
#include <limits>
#include <optional>

#include "scene.h"

namespace ioq3_map {

// A nearest-hit ray result: distance to the nearest surface (+infinity on a
// miss) and that surface's geometric normal (unnormalized; undefined on a miss).
// The normal lets the solidifier tell whether a ray enters a wall from outside.
struct ClearanceHit {
  float distance = std::numeric_limits<float>::infinity();
  Eigen::Vector3f normal = Eigen::Vector3f::Zero();
};

// A single nearest-hit ray cast against the scene, given a world-space origin
// and direction. Backed by an Embree BVH (see bvh.h). Passed to
// BuildOccluderShell so the extrusion can measure the space around a wall.
using ClearanceFn = std::function<ClearanceHit(const Eigen::Vector3f& origin,
                                               const Eigen::Vector3f& dir)>;

// Solidifies an infinitesimally thin BSP surface into a closed occluder shell
// with real thickness, so downstream consumers (path-traced baker, shadow-mapped
// renderer) see physical geometry instead of a non-physical paper-thin sheet.
//
// Unlike the visible `front` surface, the returned shell is an INDEPENDENT
// Geometry: it holds a copy of the front rim plus a back cap offset along the
// per-vertex -normal, stitched by side-wall quads along the boundary edges. The
// front's own triangles stay in `front`; the shell (back cap + side walls) plus
// the visible front together bound a watertight volume for both consumers, which
// each include the front surface anyway. The shell is flagged `occluder_only`
// and carries NO lightmap UVs, so it never consumes lightmap atlas space and
// never renders in the color pass. `front` is not modified.
//
// Spatial awareness (when `clearance` is supplied) is two independent per-vertex
// ray queries:
//   1. Depth. A ray straight back (-normal) from each vertex measures how far
//      the wall can extrude before hitting whatever is behind it; the vertex's
//      thickness is clamped to `hit - config.clearance_margin`. If any vertex
//      has no room to clear the margin, the surface is flush against geometry
//      that already occludes, and nothing is emitted (see below). (A
//      perpendicular neighbour sharing the vertex is coplanar with this ray and
//      is missed, so shared edges do not collapse the depth.)
//   2. Inward conform. From each extruded vertex, a ray toward the back-cap
//      centroid detects an inward-leaning neighbour (e.g. a trapezoidal prism's
//      slanted side): if the ray enters a wall from outside, the vertex has
//      poked through it and is pulled back in onto that wall. Right prisms are
//      untouched — their back vertices are inside, so the ray leaves the solid
//      (the entering test fails) and nothing is pulled. This is why the extruded
//      vertex, not the original one, seeds the ray: it has moved off the shared
//      edge, so it hits the slanted neighbour at t > 0 instead of t = 0.
// With `clearance` null the full `config.thickness` is used with no conforming.
//
// Returns std::nullopt (solidify nothing) when `config.thickness <= 0`, the
// surface is degenerate (< 3 vertices / 1 triangle, or mismatched normals), or
// the surface has no room to be shelled (some vertex is flush against geometry
// behind it — which already occludes, so a shell is unnecessary).
std::optional<Geometry> BuildOccluderShell(const ExtrusionConfig& config,
                                           const Geometry& front,
                                           const ClearanceFn& clearance = {});

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_EXTRUDE_H_
