bool rayCast(hh::math::CVector const& begin, hh::math::CVector const& end,
	hh::math::CVector& position, hh::math::CVector& normal, uint32_t filter
)
{
	static void* const pFunc = (void*)0x10BE3B0;

	__asm
	{
		mov		ecx, end
		push	ecx
		mov		edx, normal
		push	edx
		mov		eax, position
		push	eax
		mov		ebx, 0x1E5E2F0
		mov		ebx, [ebx]
		mov     eax, [ebx + 5ECh]
		mov     edi, [eax]
		mov		edx, filter
		mov		eax, begin

		call[pFunc]
	}
}

Surface surfaces[0x1000];
int32_t surfaceIndex = 0;

Surface* rayCast(f32 x, f32 y, f32 z, f32* pheight, const hh::math::CVector& direction, float offset = 50.0f, float length = 100000000000.0f)
{
	const hh::math::CVector begin(x * 0.01f, y * 0.01f + offset * 0.01f, z * 0.01f);
	const hh::math::CVector end = begin + direction * length * 0.01f;

	hh::math::CVector position;
	hh::math::CVector normal;

	if (!rayCast(begin, end, position, normal, 20))
		return nullptr;

	position *= 100.0f;

	normal.normalize();

	if (pheight)
		*pheight = position.y();

	Surface* surface = &surfaces[surfaceIndex++ % _countof(surfaces)];
	memset(surface, 0, sizeof(Surface));

	surface->vertex1[0] = position.x();
	surface->vertex1[1] = position.y();
	surface->vertex1[2] = position.z();

	surface->vertex2[0] = position.x();
	surface->vertex2[1] = position.y();
	surface->vertex2[2] = position.z();

	surface->vertex3[0] = position.x();
	surface->vertex3[1] = position.y();
	surface->vertex3[2] = position.z();

	surface->normal.x = normal.x();
	surface->normal.y = normal.y();
	surface->normal.z = normal.z();

	return surface;
}

extern "C" Surface* bb_find_ceil(f32 x, f32 y, f32 z, f32* pheight)
{
	return rayCast(x, y, z, pheight, hh::math::CVector::UnitY()); // Cast a ray upwards.
}

extern "C" Surface* bb_find_floor(f32 x, f32 y, f32 z, f32* pheight)
{
	Surface* surface = rayCast(x, y, z, pheight, -hh::math::CVector::UnitY()); // Cast a ray downwards.

	// NULL surface is going to cause crash. Return the previous surface instead.
	if (!surface)
		return &surfaces[(surfaceIndex - 1) % _countof(surfaces)];

	return surface;
}

extern "C" s32 bb_find_wall_collisions(struct WallCollisionData* data)
{
	static const hh::math::CVector directions[] =
	{
		-hh::math::CVector::UnitX(),
		hh::math::CVector::UnitX(),
		-hh::math::CVector::UnitZ(),
		hh::math::CVector::UnitZ(),

		hh::math::CVector(0.707f, 0, 0.707f),
		hh::math::CVector(0.707f, 0, -0.707f),
		hh::math::CVector(-0.707f, 0, 0.707f),
		hh::math::CVector(-0.707f, 0, -0.707f),
	};

	data->numWalls = 0;

	for (auto& direction : directions)
	{
		Surface* surface = rayCast(data->x, data->y, data->z, nullptr, direction, data->offsetY, data->radius);

		if (surface && abs(surface->normal.y) <= 0.1f)
		{
			if (data->numWalls > 0)
			{
				float x1 = surface->vertex1[0] - data->x;
				float y1 = surface->vertex1[1] - data->y - data->offsetY;
				float z1 = surface->vertex1[2] - data->z;

				float x2 = data->walls[0]->vertex1[0] - data->x;
				float y2 = data->walls[0]->vertex1[1] - data->y - data->offsetY;
				float z2 = data->walls[0]->vertex1[2] - data->z;

				if ((x1 * x1 + y1 * y1 + z1 * z1) > (x2 * x2 + y2 * y2 + z2 * z2))
					continue;
			}

			data->numWalls = 1;
			data->walls[0] = surface;
		}
	}
	if (data->numWalls > 0)
	{
		data->x = data->walls[0]->vertex1[0] + data->walls[0]->normal.x * data->radius;
		data->z = data->walls[0]->vertex1[2] + data->walls[0]->normal.z * data->radius;
	}

	return data->numWalls;
}