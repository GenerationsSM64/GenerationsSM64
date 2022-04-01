#include "Mod.h"
#include "Util.h"

Surface surfaces[0x1000];
int32_t surfaceIndex = 0;
constexpr f32 limit = 100000000.0f;

Surface* rayCast(f32 x, f32 y, f32 z, f32* pheight, const hh::math::CVector& direction, bool ignoreOneway = false, float offset = 50.0f, float length = limit)
{
	const hh::math::CVector begin(x * 0.01f, (y + offset) * 0.01f, z * 0.01f);
	const hh::math::CVector end = begin + direction * length * 0.01f;

	RayCastQuery query;

	if (!rayCast(*(size_t*)0x1E0AFB4, query, begin, end)) // TypePlayerTerrain
		return nullptr;

	if (ignoreOneway && query.rigidBody && *(size_t*)((char*)query.rigidBody + 12) == *(size_t*)0x1E61C30) // onewayCollisionType
		return nullptr;

	query.normal.normalize();
	query.position += query.position.cwiseAbs().cwiseProduct(query.normal.cwiseSign()) * 0.0000002f;
	query.position *= 100.0f;

	if (pheight)
		*pheight = query.position.y();

	Surface* surface = &surfaces[surfaceIndex % _countof(surfaces)];
	memset(surface, 0, sizeof(Surface));
	++surfaceIndex;

	surface->vertex1[0] = query.position.x();
	surface->vertex1[1] = query.position.y();
	surface->vertex1[2] = query.position.z();

	surface->vertex2[0] = query.position.x();
	surface->vertex2[1] = query.position.y();
	surface->vertex2[2] = query.position.z();

	surface->vertex3[0] = query.position.x();
	surface->vertex3[1] = query.position.y();
	surface->vertex3[2] = query.position.z();

	surface->normal.x = query.normal.x();
	surface->normal.y = query.normal.y();
	surface->normal.z = query.normal.z();

	return surface;
}

extern "C" f32 find_ceil(f32 posX, f32 posY, f32 posZ, struct Surface** pceil)
{
	f32 height = limit;
	*pceil = rayCast(posX, posY, posZ, &height, hh::math::CVector::UnitY(), true, 0.0f); // Cast a ray upwards.
	return height;
}

extern "C" f32 find_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface** pfloor)
{
	f32 height = -limit;
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

		case 19: // Lava
			if (!playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_InvokeFlameBarrier])
			{
				surface->type = SURFACE_BURNING;
				playerContext->m_Field164 = 0;
			}

			break;
		}

		const auto& stateName = playerContext->m_pPlayer->m_StateMachine.GetCurrentState()->GetStateName();

		if (stateName == "HipSliding")
			surface->type = SURFACE_VERY_SLIPPERY;

		else if (stateName == "OnIce")
			surface->type = SURFACE_ICE;
	}

	*pfloor = surface;
	return height;
}

bool disableWallCollision;

const hh::math::CVector WALL_DIRECTIONS_BASE[] =
{
	-hh::math::CVector::UnitX(),
	hh::math::CVector::UnitX(),
	-hh::math::CVector::UnitZ(),
	hh::math::CVector::UnitZ(),
	hh::math::CVector(0.70710678118f, 0, 0.70710678118f),
	hh::math::CVector(0.70710678118f, 0, -0.70710678118f),
	hh::math::CVector(-0.70710678118f, 0, 0.70710678118f),
	hh::math::CVector(-0.70710678118f, 0, -0.70710678118f),
};

hh::math::CVector wallDirections[_countof(WALL_DIRECTIONS_BASE)] =
{
	WALL_DIRECTIONS_BASE[0],
	WALL_DIRECTIONS_BASE[1],
	WALL_DIRECTIONS_BASE[2],
	WALL_DIRECTIONS_BASE[3],
	WALL_DIRECTIONS_BASE[4],
	WALL_DIRECTIONS_BASE[5],
	WALL_DIRECTIONS_BASE[6],
	WALL_DIRECTIONS_BASE[7],
};

float wallDirectionCurrentFaceAngle;

extern "C" s32 find_wall_collisions(struct WallCollisionData* data)
{
	if (disableWallCollision)
		return 0;

	if (abs(state.faceAngle - wallDirectionCurrentFaceAngle) > 0.001f)
	{
		hh::math::CQuaternion orientation;
		orientation = Eigen::AngleAxisf(state.faceAngle, Eigen::Vector3f::UnitY());

		for (size_t i = 0; i < _countof(WALL_DIRECTIONS_BASE); i++)
			wallDirections[i] = orientation * WALL_DIRECTIONS_BASE[i];

		wallDirectionCurrentFaceAngle = state.faceAngle;
	}

	hh::math::CVector directionCurr;

	// New version of the wall detection code fixes BLJs, somehow.
	// Execute the old code when long jumping to bring BLJs back.
	//
	// Please note that it's possible the old code simply was making
	// BLJs way easier to pull off, but honestly, it's fun!
	bool bljTolerantCurr = FALSE;

	data->numWalls = 0;

	for (auto& direction : wallDirections)
	{
		Surface* surface = rayCast(data->x, data->y, data->z, nullptr, direction, true, data->offsetY, data->radius);

		if (!surface)
			continue;

		const bool bljTolerant = g_state->mgMarioStateVal.action == ACT_LONG_JUMP &&
			wallDirections[3].dot(hh::math::CVector(surface->normal.x, surface->normal.y, surface->normal.z)) > 0;

		// Do the new checks only if we aren't BLJ tolerant.
		if (!bljTolerant && abs(surface->normal.y) > 0.1f)
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
		directionCurr = direction;
		bljTolerantCurr = bljTolerant;
	}

	if (data->numWalls > 0)
	{
		Surface* wall = data->walls[0];

		// Apply a bias alongside the ray we cast. Using the surface
		// normal instead causes Mario to "wall slide" for some reason.

		// For BLJ tolerant surfaces, make an exception and use
		// the old code where sliding still happens.

		float x = bljTolerantCurr ? wall->normal.x : -directionCurr.x();
		float z = bljTolerantCurr ? wall->normal.z : -directionCurr.z();

		data->x = wall->vertex1[0] + x * data->radius;
		data->z = wall->vertex1[2] + z * data->radius;
	}

	return data->numWalls;
}

f32 waterLevel;

extern "C" f32 find_water_level(f32 x, f32 y, f32 z)
{
	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();

	if (playerContext && !playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_OnWater])
		waterLevel = -limit;

	return waterLevel;
}

HOOK(void, __stdcall, EnterWater, 0xE55260, Sonic::Player::CPlayerSpeedContext* playerContext, bool isNoDeadWater)
{
	waterLevel = playerContext->m_spMatrixNode->m_Transform.m_Position.y() * 100.0f;
	originalEnterWater(playerContext, isNoDeadWater);
}

void initCollision()
{
	INSTALL_HOOK(EnterWater);
}