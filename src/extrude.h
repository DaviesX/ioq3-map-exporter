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
// `config.inset` (meters) pulls the back rim toward the surface centroid so the
// side walls tilt slightly inward and never land coplanar with a neighbouring
// face's front cap (e.g. the parallel sides of a cube), which would z-fight.
//
// All new geometry is appended into the same `geo`, preserving the one-Geometry
// -> one-glTF-primitive mapping that the manifest relies on. No-op when
// `config.thickness <= 0` or the surface has fewer than 3 vertices / one
// triangle.
void SolidifyGeometry(const ExtrusionConfig& config, Geometry* geo);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_EXPORTER_EXTRUDE_H_
