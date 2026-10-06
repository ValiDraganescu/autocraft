// The ground's heights on the terrain mesh's own grid: a port of
// `HeightGrid` (Sources/Autocraft/GameScene+Rays.swift). Plain C++ on the
// core's `ac::TerrainField`, with no engine types, so the ray code (`AcRays`,
// chunk E1) and its tests can use it without Unreal.
//
// The grid is the one `AAcTerrain` draws: `Density` vertices per cell from
// the map bounds' min corner, every grid square split along the diagonal
// from (i+1, j) to (i, j+1), exactly as `TerrainBuilder.build` splits it. So
// `height(p)` is the height of the drawn triangle under `p`, not the field's
// smooth height: what the eye sees is what a ray meets.
//
// Coordinates are the sim's: cells, ground point `ac::Vec2(x, y)`, height up.
// `crossing` takes points as SceneKit does, (x, height, y), so the Swift ray
// code ports line by line; convert Unreal points with `AcSpace::ToSceneKit`.
#pragma once

#include "TerrainField.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

struct FAcHeightGrid
{
	/// Mesh vertices per cell along each axis (`TerrainBuilder.density`).
	static constexpr int density = 4;

	/// A point as the Swift code holds it: `SIMD3<Float>` (x, height, z).
	struct F3
	{
		float x = 0, y = 0, z = 0;
	};

	/// Runs `body(j)` for every j in [0, n), in any order and on any thread
	/// (Unreal passes `ParallelFor`); empty: a plain loop.
	using ParallelRows = std::function<void(int64_t n, const std::function<void(int64_t)>& body)>;

	/// Not owned: the terrain keeps the field alive as long as the grid.
	const ac::TerrainField* field = nullptr;
	double minX = 0, minZ = 0, step = 1.0 / density;
	int64_t nx = 0, nz = 0;
	std::vector<float> heights;

	FAcHeightGrid() = default;
	/// Samples `field.height` at every grid vertex.
	FAcHeightGrid(const ac::TerrainField& field, int density = FAcHeightGrid::density, const ParallelRows& rows = {});

	bool empty() const { return heights.empty(); }

	/// The height at vertex (i, j), clamped to the grid.
	float at(int64_t i, int64_t j) const
	{
		const int64_t jj = j < 0 ? 0 : (j > nz - 1 ? nz - 1 : j);
		const int64_t ii = i < 0 ? 0 : (i > nx - 1 ? nx - 1 : i);
		return heights[static_cast<size_t>(jj * nx + ii)];
	}

	/// Ground point of vertex (i, j).
	ac::Vec2 point(int64_t i, int64_t j) const { return ac::Vec2(minX + double(i) * step, minZ + double(j) * step); }

	/// The mesh's height at `p`: each square is split as `TerrainBuilder`
	/// splits it, along the diagonal from (i+1, j) to (i, j+1). Off the grid
	/// (the scenery past the map's edge), the field's border height.
	double height(ac::Vec2 p) const;

	/// How far along the segment from `a` to `b` (0…1) it first goes below
	/// the ground; nil when it stays above. Marched a quarter of a grid
	/// square at a time, then halved down onto the crossing.
	std::optional<float> crossing(F3 a, F3 b) const;
};
