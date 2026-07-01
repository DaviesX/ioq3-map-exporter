#ifndef IOQ3_MAP_EXPORTER_EXTRUDE_H_
#define IOQ3_MAP_EXPORTER_EXTRUDE_H_

#include <Eigen/Dense>  // IWYU pragma: keep
#include <functional>
#include <optional>

#include "scene.h"

namespace ioq3_map {

// A single nearest-hit ray cast: given a world-space ray origin and direction,
// returns the distance to the nearest surface (or +infinity if none). Backed by
// an Embree BVH (see bvh.h). Passed to BuildOccluderShell so the extrusion can
// clamp itself to the free space actually available behind a wall.
using ClearanceFn = std::function<float(const Eigen::Vector3f& origin,
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
// Spatial awareness: when `clearance` is supplied, the shell thickness is
// clamped to `d - config.clearance_margin`, where `d` is the backward clearance
// (nearest surface along a small cone about -normal), so it stops short of
// whatever is behind the wall. Clearance is measured from face-INTERIOR sample
// points, not the rim, so a wall's shared edges/corners (which lie on its
// neighbouring faces) can never collapse the thickness to zero. The result is
// floored at `config.min_thickness`, so a shell is always non-degenerate — a
// wall with no room behind it still gets a real, if thin, shell. With
// `clearance` null the full `config.thickness` is used.
//
// `config.inset` (meters, clamped to the local thickness) pushes each back-rim
// vertex perpendicularly into the surface interior (per boundary edge, not
// toward the global centroid) so side walls tilt off neighbouring faces' planes
// and never land coplanar with a neighbour (e.g. the parallel sides of a cube),
// which would z-fight.
//
// Returns std::nullopt (solidify nothing) only when `config.thickness <= 0` or
// the surface is degenerate (< 3 vertices / 1 triangle, or mismatched normals).
// A valid surface always yields a shell (thickness floored at min_thickness).
std::optional<Geometry> BuildOccluderShell(const ExtrusionConfig& config,
                                           const Geometry& front,
                                           const ClearanceFn& clearance = {});

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_EXTRUDE_H_
