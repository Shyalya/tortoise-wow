#ifndef DC_COMPAT_PATHGENERATOR_H
#define DC_COMPAT_PATHGENERATOR_H

// AzerothCore's PathGenerator over Penqle's PathInfo.
//
// Same job, different spelling and a different return type for the path: AC
// hands back a Movement::PointsArray of G3D::Vector3, and so does PathInfo, so
// the wrapper is thin. The four methods below are all the module uses.
//
// Path type constants differ in name but not in meaning; the enum values are
// mapped one for one so a PATHFIND_NOPATH test reads the same on both.

#include "Maps/PathFinder.h"

class PathGenerator
{
    public:
        explicit PathGenerator(Unit const* owner) : m_path(owner) {}
        PathGenerator(uint32 mapId, uint32 instanceId) : m_path(mapId, instanceId) {}

        bool CalculatePath(float destX, float destY, float destZ, bool forceDest = false)
        {
            return m_path.calculate(destX, destY, destZ, forceDest);
        }

        // mod-playerbots also passes an explicit start; PathInfo has the same
        // form under vectors.
        bool CalculatePath(float sx, float sy, float sz, float dx, float dy, float dz, bool forceDest = false)
        {
            return m_path.calculate(G3D::Vector3(sx, sy, sz), G3D::Vector3(dx, dy, dz), forceDest);
        }

        PathType GetPathType() const { return m_path.getPathType(); }

        Movement::PointsArray const& GetPath() const { return m_path.getPath(); }

        G3D::Vector3 GetActualEndPosition() const { return m_path.getActualEndPosition(); }

        // AzerothCore sums this while building; PathInfo does not carry it, so
        // it is summed here from the points - same number, same unit.
        float getPathLength() const
        {
            Movement::PointsArray const& pts = m_path.getPath();
            float len = 0.0f;
            for (size_t i = 1; i < pts.size(); ++i)
                len += (pts[i] - pts[i - 1]).length();
            return len;
        }
        float GetPathLength() const { return getPathLength(); }

        void SetUseStraightPath(bool v) { m_path.setUseStrightPath(v); }
        void SetPathLengthLimit(float d) { m_path.setPathLengthLimit(d); }

    private:
        PathInfo m_path;
};

#endif
