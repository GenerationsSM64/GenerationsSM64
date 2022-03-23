#include "Mod.h"

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
constexpr f32 limit = 100000000.0f;

Surface* rayCast(f32 x, f32 y, f32 z, f32* pheight, const hh::math::CVector& direction, int flags = 20, float offset = 50.0f, float length = limit)
{
	const hh::math::CVector begin(x * 0.01f, y * 0.01f + offset * 0.01f, z * 0.01f);
	const hh::math::CVector end = begin + direction * length * 0.01f;

	hh::math::CVector position;
	hh::math::CVector normal;

	if (!rayCast(begin, end, position, normal, flags) || direction.dot(normal) >= 0)
		return nullptr;

	normal.normalize();
	position += position.cwiseAbs().cwiseProduct(normal.cwiseSign()) * 0.0000002f;
	position *= 100.0f;

	if (pheight)
		*pheight = position.y();

	Surface* surface = &surfaces[surfaceIndex % _countof(surfaces)];
	memset(surface, 0, sizeof(Surface));
	++surfaceIndex;

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

extern "C" f32 find_ceil(f32 posX, f32 posY, f32 posZ, struct Surface** pceil)
{
	f32 height = CELL_HEIGHT_LIMIT;
	*pceil = rayCast(posX, posY, posZ, &height, hh::math::CVector::UnitY()); // Cast a ray upwards.
	return height;
}

extern "C" f32 find_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface** pfloor)
{
	f32 height = FLOOR_LOWER_LIMIT;
	Surface* surface = rayCast(xPos, yPos, zPos, &height, -hh::math::CVector::UnitY()); // Cast a ray downwards.

	// NULL surface is going to cause crash. Create a dummy surface instead.
	if (!surface)
	{
		surface = &surfaces[surfaceIndex % _countof(surfaces)];
		memset(surface, 0, sizeof(Surface));
		++surfaceIndex;

		surface->vertex1[0] = limit;
		surface->vertex1[1] = -limit;
		surface->vertex1[2] = limit;

		surface->vertex2[0] = limit;
		surface->vertex2[1] = -limit;
		surface->vertex2[2] = limit;

		surface->vertex3[0] = limit;
		surface->vertex3[1] = -limit;
		surface->vertex3[2] = limit;

		surface->normal.x = 0.0f;
		surface->normal.y = 1.0f;
		surface->normal.z = 0.0f;

		height = -limit;
	}

	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();
	if (playerContext)
	{
		switch (playerContext->m_Field164)
		{
		case 2: // Water
		case 11:
		case 14:
			surface->terrain = TERRAIN_WATER;
			break;

		case 3: // Rock
			surface->terrain = TERRAIN_STONE;
			break;

		case 4: // Dirt
		case 9: // Sand
			surface->terrain = TERRAIN_GRASS;
			break;

		case 5: // Wood
			surface->terrain = TERRAIN_SPOOKY;
			break;

		case 6: // Grass
			surface->terrain = TERRAIN_GRASS;
			surface->type = SURFACE_NOISE_DEFAULT;
			break;
		}
	}

	*pfloor = surface;
	return height;
}

extern "C" s32 find_wall_collisions(struct WallCollisionData* data)
{
	hh::math::CQuaternion orientation;
	orientation = Eigen::AngleAxisf(state.faceAngle, Eigen::Vector3f::UnitY());

	const hh::math::CVector directions[] =
	{
		orientation * -hh::math::CVector::UnitX(),
		orientation * hh::math::CVector::UnitX(),
		orientation * -hh::math::CVector::UnitZ(),
		orientation * hh::math::CVector::UnitZ(),
		orientation * hh::math::CVector(0.707f, 0, 0.707f),
		orientation * hh::math::CVector(0.707f, 0, -0.707f),
		orientation * hh::math::CVector(-0.707f, 0, 0.707f),
		orientation * hh::math::CVector(-0.707f, 0, -0.707f),
	};

	hh::math::CVector direction;

	data->numWalls = 0;

	for (auto& dir : directions)
	{
		Surface* surface = rayCast(data->x, data->y, data->z, nullptr, dir, 20, data->offsetY, data->radius);

		if (!surface)
			continue;

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
		direction = dir;
	}

	if (data->numWalls > 0)
	{
		Surface* wall = data->walls[0];

		data->x = wall->vertex1[0] - direction.x() * data->radius;
		data->z = wall->vertex1[2] - direction.z() * data->radius;
	}

	return data->numWalls;
}

extern "C" f32 find_water_level(f32 x, f32 y, f32 z)
{
	static f32 waterLevel;

	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();

	if (playerContext && playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_OnWater])
	{
		if (waterLevel == -limit)
			waterLevel = y;
	}

	else
		waterLevel = -limit;

	return waterLevel;
}