#pragma once

// provided by Brian
enum CollisionType : uint32_t
{
	TypeNoAttack = 0x1E61B5C,
	TypeRagdoll = 0x1E61B60,
	TypeSonicSpinCharge = 0x1E61B64,
	TypeSonicSpin = 0x1E61B68,
	TypeSonicUnbeaten = 0x1E61B6C,
	TypeSuperSonic = 0x1E61B70,
	TypeSonicSliding = 0x1E61B74,
	TypeSonicHoming = 0x1E61B78,
	TypeSonicSelectJump = 0x1E61B7C,
	TypeSonicDrift = 0x1E61B80,
	TypeSonicBoost = 0x1E61B84,
	TypeSonicStomping = 0x1E61B88,
	TypeSonicTrickAttack = 0x1E61B8C,
	TypeSonicSquatKick = 0x1E61B90,
	TypeSonicClassicSpin = 0x1E61B94,
	TypeExplosion = 0x1E61B98,
	TypeBossAttack = 0x1E61B9C,
	TypeGunTruckAttack = 0x1E61BA0,
	TypeRagdollEnemyAttack = 0x1E61BA4,
};

struct RayCastQuery
{
	Eigen::Vector3f normal;
	Eigen::Vector3f position;
	Sonic::CRigidBody* rigidBody;
	float field1C;
	size_t field20;
	bool valid;
};

std::unique_ptr<uint8_t[]> readAllBytes(const std::string& filePath, size_t& size);

void* setCollision(CollisionType collisionType, bool enabled);
void getPathControllerData(Sonic::CPathController* controller, hh::math::CVector* point, hh::math::CVector* invec, hh::math::CVector* outvec);

static inline FUNCTION_PTR(bool, __stdcall, rayCastPlayerTerrain, 0xE58140, Sonic::Player::CPlayerContext* playerContext, RayCastQuery& query, 
	const hh::math::CVector& begin, const hh::math::CVector& end);

bool rayCast(size_t collisionType, RayCastQuery& query, const hh::math::CVector& begin, const hh::math::CVector& end);
bool rigidBodyHasProperty(Sonic::CRigidBody* rigidBody, size_t property, bool& enabled);