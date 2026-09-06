// Production flight generator/startup hook and edge publication, with
// deterministic graph/DBC services. No realm, database, or flight simulator.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x << '\n'; std::exit(1); } } while (0)
constexpr float PLAYERBOT_TAXI_ROUTE_DIVISOR = 3600.0f;
struct WorldPosition
{
    uint32 map;
    float x, y, z;
    WorldPosition(uint32 m = 0, float a = 0, float b = 0, float c = 0, float = 0)
        : map(m), x(a), y(b), z(c) {}
    float getPathLength(std::vector<WorldPosition> const& points) const {
        float length = 0;
        for (size_t i = 1; i < points.size(); ++i) length += points[i-1].distance(points[i]);
        return length;
    }
    float distance(WorldPosition const& p) const
    {
        if (map != p.map) return 1e20f;
        return std::sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z));
    }
};
enum class TravelNodePathType : uint8 { none, walk, areaTrigger, transport, flightPath, teleportSpell, staticPortal };
struct TravelNodePath
{
    float distance, extraCost;
    TravelNodePathType pathType;
    uint64 object;
    bool calculated;
    std::vector<WorldPosition> path;
    TravelNodePath(float d = .1f, float cost = 0, uint8 type = 1, uint64 id = 0, bool calc = false)
        : distance(d), extraCost(cost), pathType(TravelNodePathType(type)), object(id), calculated(calc) {}
    TravelNodePathType getPathType() { return pathType; }
    uint64 getPathObject() { return object; }
    void setPath(std::vector<WorldPosition> p) { path = std::move(p); }
    void calculateCost(bool onlyDistance)
    {
        CHECK(onlyDistance && !calculated);
        distance = .1f;
        for (size_t i = 1; i < path.size(); ++i)
            if (path[i-1].map == path[i].map) distance += path[i-1].distance(path[i]);
    }

};
std::vector<std::string> events;
struct TravelNode
{
    WorldPosition point;
    std::unordered_map<TravelNode*, TravelNodePath> paths;
    std::unordered_map<TravelNode*, TravelNodePath*> links;
    WorldPosition* getPosition() { return &point; }
    float fDist(WorldPosition p) { return point.distance(p); }
    void hasRouteTo(TravelNode*) { events.push_back("coverage"); }
#include "NativeTravelSetPath.inc"
};
struct TaxiPathEntry { uint32 ID, from, to, price; };
struct TaxiNodesEntry { uint32 map_id; float x, y, z; };
struct TaxiPathNodeEntry { uint32 mapid; float x, y, z; };
#include "NativeTaxiNodePtr.inc"
using TaxiPathNodeList = std::deque<TaxiPathNodePtr>;
template<class T> struct Store
{
    std::map<uint32, T> rows;
    uint32 GetNumRows() { return rows.empty() ? 0 : rows.rbegin()->first + 1; }
    T const* LookupEntry(uint32 id) { auto it = rows.find(id); return it == rows.end() ? nullptr : &it->second; }
};
Store<TaxiPathEntry> sTaxiPathStore;
Store<TaxiNodesEntry> sTaxiNodesStore;
std::vector<TaxiPathNodeList> sTaxiPathNodesByPath;
struct Log
{
    uint32 generated = 0, corrected = 0, incomplete = 0, summaries = 0;
    void outString(char const*) {}
    void outString(char const*, uint32 g, uint32 c, uint32 i)
    { generated = g; corrected = c; incomplete = i; ++summaries; events.push_back("taxi"); }
} sLog;
struct { void LoadMapTransfers() { events.push_back("transfers"); } } sTravelMgr;
struct TravelNodeMap
{
    bool hasToGen = false, hasToFullGen = false, hasToSave = false;
    std::vector<TravelNode*> graph;
    void LoadMaps() { events.push_back("maps"); }
    void generateNodes() { events.push_back("nodes"); }
    void calcMapOffset() { events.push_back("offsets"); }
    void generatePaths(bool helpers) { CHECK(!helpers); events.push_back("walking"); generateTaxiPaths(); }
    std::vector<TravelNode*> getNodes() { return graph; }
    TravelNode* getNode(WorldPosition p, std::nullptr_t, float range)
    {
        TravelNode* nearest = nullptr;
        for (auto node : graph)
        {
            float const d = node->point.distance(p);
            if (d <= range) { range = d; nearest = node; }
        }
        return nearest;
    }
    void generateTaxiPaths();
    void generateAll();
} sTravelNodeMap;
#include "NativeTaxiGeneration.inc"
#include "NativeTravelGeneration.inc"

int main()
{
    // Live failure: the bundled Southshore->Menethil ID 272 means 71->5 in
    // current DBC data, whereas the correct 14->7 ID is 99. ID order is sparse.
    TravelNode shore{{0, -711.48f, -515.48f, 26.11f}}, harbor{{0, -3793.2f, -782.05f, 9.66f}};
    TravelNode otherMap{{1, -711.48f, -515.48f, 26.11f}}, elsewhere{{0, 20000, 0, 0}};
    sTravelNodeMap.graph = {&otherMap, &shore, &harbor, &elsewhere};
    sTaxiNodesStore.rows = {{14, {0, shore.point.x, shore.point.y, shore.point.z}},
        {7, {0, harbor.point.x, harbor.point.y, harbor.point.z}},
        {71, {0, 10000, 0, 0}}, {5, {0, 11000, 0, 0}}};
    sTaxiPathStore.rows = {{99, {99, 14, 7, 200}}, {98, {98, 7, 14, 200}}, {272, {272, 71, 5, 20}}};
    TaxiPathNodeEntry a{0, -711, -510, 40}, b{0, -1500, -500, 80}, c{0, -3790, -782, 20};
    sTaxiPathNodesByPath.resize(273);
    sTaxiPathNodesByPath[99] = {&a, &b, &c};
    sTaxiPathNodesByPath[98] = {&c, &b, &a};
    shore.setPathTo(&harbor, TravelNodePath(.1f, 999, 4, 272, true));
    harbor.setPathTo(&shore, TravelNodePath(.1f, 999, 4, 271, true));
    // Unrelated walk/boat/portal/spell-click links must not be reset or removed.
    shore.setPathTo(&elsewhere, TravelNodePath(300, 0, 1));
    otherMap.setPathTo(&shore, TravelNodePath(80, 10, 3, 12345));
    elsewhere.setPathTo(&otherMap, TravelNodePath(10, 5, 6, 987));
    harbor.setPathTo(&otherMap, TravelNodePath(10, 7, 4, 900001));
    sTravelNodeMap.generateAll();
    CHECK(sLog.generated == 2 && sLog.corrected == 2 && sLog.incomplete == 0);
    CHECK(!sTravelNodeMap.hasToSave);
    CHECK(std::find(events.begin(), events.end(), "walking") == events.end());
    CHECK(std::find(events.begin(), events.end(), "maps") == events.end());
    CHECK(std::find(events.begin(), events.end(), "taxi") < std::find(events.begin(), events.end(), "coverage"));
    auto const& path = shore.paths.at(&harbor);
    CHECK(path.object == 99 && path.pathType == TravelNodePathType::flightPath && path.calculated);
    CHECK(path.path.size() == 5 && path.path[1].distance({a.mapid,a.x,a.y,a.z}) == 0);
    CHECK(path.path.front().distance(shore.point) == 0 && path.path.back().distance(harbor.point) == 0);
    CHECK(path.extraCost > 0 && path.extraCost == shore.point.getPathLength(path.path) / PLAYERBOT_TAXI_ROUTE_DIVISOR);
    CHECK(shore.links.at(&harbor) == &shore.paths.at(&harbor));
    CHECK(harbor.paths.at(&shore).object == 98);
    CHECK(otherMap.paths.size() == 1 && otherMap.paths.at(&shore).object == 12345);
    CHECK(shore.paths.at(&elsewhere).pathType == TravelNodePathType::walk);
    CHECK(elsewhere.paths.at(&otherMap).object == 987 && harbor.paths.at(&otherMap).object == 900001);
    auto const initialSize = shore.paths.size();
    for (unsigned i = 0; i < 100; ++i)
    {
        sTravelNodeMap.generateAll();
        CHECK(sLog.corrected == 0 && shore.paths.size() == initialSize && !sTravelNodeMap.hasToSave);
        CHECK(shore.links.at(&harbor) == &shore.paths.at(&harbor));
    }
    // Partial and full generation already call the flight generator: do not
    // duplicate it, and retain the native save and map-generation lifecycle.
    for (bool full : {false, true})
    {
        sTravelNodeMap.hasToGen = !full; sTravelNodeMap.hasToFullGen = full;
        sTravelNodeMap.hasToSave = false; events.clear(); unsigned before = sLog.summaries;
        sTravelNodeMap.generateAll();
        CHECK(sLog.summaries == before + 1 && sTravelNodeMap.hasToSave);
        CHECK(!sTravelNodeMap.hasToGen && !sTravelNodeMap.hasToFullGen);
        CHECK(std::count(events.begin(), events.end(), "walking") == 1);
        CHECK(std::count(events.begin(), events.end(), "nodes") == unsigned(full));
    }
    // No null dereference or partially published replacement for missing or
    // sparse geometry; unrelated cached edges remain intact for diagnosis.
    for (unsigned mode = 0; mode < 5; ++mode)
    {
        sTaxiPathStore.rows = {{99, {99, 14, 7, 200}}};
        sTaxiPathNodesByPath.resize(100);
        sTaxiPathNodesByPath[99] = {&a, &b, &c};
        if (mode < 3) sTaxiPathNodesByPath[99][mode] = TaxiPathNodePtr{};
        if (mode == 3) sTaxiPathNodesByPath[99].clear();
        if (mode == 4) sTaxiPathNodesByPath.clear();
        sTravelNodeMap.generateTaxiPaths();
        CHECK(sLog.generated == 0 && sLog.incomplete == 1);
        CHECK(shore.paths.at(&harbor).object == 99 && shore.paths.at(&harbor).path.size() == 5);
    }
    // Absent endpoints, absent graph nodes, same-node paths, empty stores.
    sTaxiPathNodesByPath.resize(100); sTaxiPathNodesByPath[99] = {&a, &b, &c};
    for (auto pair : std::vector<std::pair<uint32,uint32>>{{999,7},{14,999},{14,14},{71,5}})
    {
        sTaxiPathStore.rows[99] = {99,pair.first,pair.second,200};
        sTravelNodeMap.generateTaxiPaths(); CHECK(sLog.generated == 0);
    }
    sTaxiPathStore.rows.clear(); sTravelNodeMap.generateTaxiPaths();
    CHECK(sLog.generated == 0 && shore.paths.at(&harbor).object == 99);
    std::cout << "Native cached-flight refresh/lifecycle and malformed-data tests passed\n";
}
