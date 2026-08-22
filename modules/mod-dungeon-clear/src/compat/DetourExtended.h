#ifndef DC_COMPAT_DETOUREXTENDED_H
#define DC_COMPAT_DETOUREXTENDED_H

// AzerothCore extends Detour with dtQueryFilterExt, a filter with virtual cost
// hooks. Stock Detour has the same behind DT_VIRTUAL_QUERYFILTER, which this
// build defines globally (top-level CMakeLists, ODR note there). A real class
// rather than a using-alias: module headers forward-declare
// `class dtQueryFilterExt;`, and a forward declaration cannot name an alias.
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

class dtQueryFilterExt : public dtQueryFilter
{
};

#endif
