#ifndef IOQ3_MAP_EXPORTER_EXTRUDE_H_
#define IOQ3_MAP_EXPORTER_EXTRUDE_H_

#include "scene.h"

namespace ioq3_map {

// Solidifies an infinitesimally thin BSP surface into a closed shell with real
// thickness, so downstream consumers (path-traced baker, shadow-mapped renderer)
// see physical geometry instead of a non-physical paper-thin sheet.
//
// The original (front) triangles are left untouched and visible. A back cap is
// appended, offset by `thickness` (meters) along the per-vertex -normal, plus
// side-wall quads along the surface's boundary edges, forming a watertight shell.
//
// `config.inset` (meters) pushes each back-rim vertex perpendicularly into the
// surface interior (per boundary edge, not toward the global centroid) so every
// side wall tilts off neighbouring faces' planes and never lands coplanar with a
// neighbour's front cap (e.g. the parallel sides of a cube), which would z-fight.
// The per-edge direction clears elongated/oblique edges that a centroid pull
// leaves nearly coplanar; the pull is clamped by the centroid distance so small
// surfaces can't overshoot.
//
// All new geometry is appended into the same `geo`, preserving the one-Geometry
// -> one-glTF-primitive mapping that the manifest relies on. No-op when
// `config.thickness <= 0` or the surface has fewer than 3 vertices / one
// triangle.
void SolidifyGeometry(const ExtrusionConfig& config, Geometry* geo);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_EXTRUDE_H_
