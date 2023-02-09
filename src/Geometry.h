// z-pilot - every visible object is generated here. No models, no sprites.
#pragma once
#include "Field.h"
#include "Renderer.h"

namespace zp {

struct MeshData {
    std::vector<Vtx> v;

    void triN(Vec3 a, Vec3 b, Vec3 c, Vec3 n, Vec3 col, float ga = 0, float gb = 0, float gc = 0) {
        v.push_back({a, n, col, ga});
        v.push_back({b, n, col, gb});
        v.push_back({c, n, col, gc});
    }
    void tri(Vec3 a, Vec3 b, Vec3 c, Vec3 col, float ga = 0, float gb = 0, float gc = 0) {
        triN(a, b, c, normalize(cross(b - a, c - a)), col, ga, gb, gc);
    }
};

std::vector<Vec2> RegularPoly(int sides, float radius, float phase = 0.0f);

// poly must be counter clockwise; zFront is nearer the camera than zBack.
void ExtrudePoly(MeshData& m, const std::vector<Vec2>& poly, float zFront, float zBack,
                 Vec3 faceCol, Vec3 sideCol, float glow);
void BiPyramid(MeshData& m, const std::vector<Vec2>& poly, float zFront, float zBack, Vec3 col,
               float glow);

// The planet slice, built as two sheets over the same grid that share the
// cavern isoline exactly. BuildTerrain is the dull matte cut face; BuildCave is
// the lit, veined bore, and it carries the hard collision boundary ribbon.
MeshData BuildTerrain(const Field& f, uint32_t seed);
MeshData BuildCave(const Field& f, uint32_t seed);
MeshData BuildShip();
MeshData BuildPod();
MeshData BuildPedestal();
MeshData BuildFuelCell();
MeshData BuildTurretBase();
MeshData BuildTurretBarrel();
MeshData BuildReactorRing();
MeshData BuildReactorCore();
MeshData BuildBullet();
MeshData BuildMine();
MeshData BuildDrone();
MeshData BuildDroneEye();
MeshData BuildWellRing();
MeshData BuildWellCore();

// Physics props. All are built at unit size and scaled per instance, so one
// mesh covers every drum, crate and slab the generator asks for.
MeshData BuildTank();
MeshData BuildTankCore();
MeshData BuildCrate();
MeshData BuildBoulder();
MeshData BuildSlab();
MeshData BuildRelic();

}  // namespace zp
