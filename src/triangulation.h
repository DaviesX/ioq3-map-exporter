#ifndef IOQ3_MAP_TRIANGULATION_H_
#define IOQ3_MAP_TRIANGULATION_H_

#include "bsp_geometry.h"

namespace ioq3_map {

// Triangulates a convex polygon into a triangle mesh using a triangle fan.
BSPMesh Triangulate(const BSPPolygon& polygon);

// Triangulates a quadratic Bezier patch into a grid mesh.
BSPMesh Triangulate(const BSPPatch& patch, int subdivisions = 7);

}  // namespace ioq3_map

#endif  // IOQ3_MAP_TRIANGULATION_H_
