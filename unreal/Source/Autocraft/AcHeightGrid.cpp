// Port of `HeightGrid` (Sources/Autocraft/GameScene+Rays.swift).
#include "AcHeightGrid.h"

#include <algorithm>
#include <cmath>

FAcHeightGrid::FAcHeightGrid(const ac::TerrainField& field_, const int density_, const ParallelRows& rows)
	: field(&field_)
{
	const ac::GroundRect b = field_.map.bounds;
	step = 1.0 / double(density_);
	nx = int64_t(std::ceil(b.width() / step)) + 1;
	nz = int64_t(std::ceil(b.depth() / step)) + 1;
	minX = b.minX;
	minZ = b.minZ;
	heights.assign(static_cast<size_t>(nx * nz), 0.0f);
	const std::function<void(int64_t)> row = [this](const int64_t j) {
		const double z = minZ + double(j) * step;
		for (int64_t i = 0; i < nx; i++)
		{
			heights[static_cast<size_t>(j * nx + i)] = float(field->height(ac::Vec2(minX + double(i) * step, z)));
		}
	};
	if (rows)
	{
		rows(nz, row);
	}
	else
	{
		for (int64_t j = 0; j < nz; j++) row(j);
	}
}

double FAcHeightGrid::height(const ac::Vec2 p) const
{
	const double x = (p.x - minX) / step, z = (p.y - minZ) / step;
	if (!(x >= 0 && z >= 0 && x <= double(nx - 1) && z <= double(nz - 1))) return field->borderHeight(p);
	const int64_t i = std::min(int64_t(x), nx - 2), j = std::min(int64_t(z), nz - 2);
	const float fx = float(x - double(i)), fz = float(z - double(j));
	const float a = at(i, j), b = at(i + 1, j), c = at(i, j + 1);
	if (fx + fz <= 1) return double(a + (b - a) * fx + (c - a) * fz);
	const float d = at(i + 1, j + 1);
	return double(d + (c - d) * (1 - fx) + (b - d) * (1 - fz));
}

std::optional<float> FAcHeightGrid::crossing(const F3 a, const F3 b) const
{
	const auto below = [&](const float t) {
		const F3 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
		return double(p.y) < height(ac::Vec2(double(p.x), double(p.z)));
	};
	if (below(0)) return 0.0f;
	const float dx = b.x - a.x, dz = b.z - a.z;
	const double run = double(std::sqrt(dx * dx + dz * dz));
	const int64_t n = std::max<int64_t>(1, int64_t(std::ceil(run / (step / 4))));
	float lo = 0;
	for (int64_t k = 1; k <= n; k++)
	{
		const float t = float(k) / float(n);
		if (!below(t))
		{
			lo = t;
			continue;
		}
		float hi = t;
		for (int r = 0; r < 12; r++)
		{
			const float mid = (lo + hi) / 2;
			if (below(mid)) hi = mid;
			else lo = mid;
		}
		return hi;
	}
	return std::nullopt;
}
