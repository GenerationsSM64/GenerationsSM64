#include "Mod.h"

boost::shared_ptr<Hedgehog::Sound::CSoundHandle> soundHandles[16];
int soundHandleCues[_countof(soundHandles)];
bool soundHandleFlags[_countof(soundHandles)];

extern "C" void play_sound(s32 soundBits, f32* pos)
{
	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();
	if (playerContext)
	{
		const size_t cue = soundBits & 0xF0FF0000;
		const size_t bank = soundBits >> 28;

		if (cue != soundHandleCues[bank])
			soundHandles[bank] = playerContext->PlaySound(cue, false);

		soundHandleCues[bank] = cue;
		soundHandleFlags[bank] = true;
	}
}

hh::math::CMatrix overrideMatrix;
bool useOverrideMatrix;

extern "C" f32* geo_get_override_matrix()
{
	return useOverrideMatrix ? overrideMatrix.data() : nullptr;
}

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

static void* setCollision(CollisionType collisionType, bool enabled)
{
    static void* const pEnableFunc = (void*)0xE65610;
    static void* const pDisableFunc = (void*)0xE655C0;

    __asm
    {
        mov edi, 0x1E5E2F0
        mov edi, [edi]

        mov ecx, collisionType
        mov ecx, [ecx]
        push ecx

        cmp enabled, 0
        je jump

        call [pEnableFunc]
        jmp end

    jump:
        call [pDisableFunc]
        
        end:
    }
}

int32_t mario = -1;
SM64MarioInputs inputs;
SM64MarioState state;
SM64MarioGeometryBuffers buffers;
boost::shared_ptr<hh::mr::CModelData> modelData;
boost::shared_ptr<hh::mr::CSingleElement> renderable;
size_t previousTriangleCount;

void deleteMario()
{
	if (mario >= 0)
		sm64_mario_delete(mario);

	mario = -1;
	modelData.reset();
	renderable.reset();
}

void computeMarioPositionAndRotation(hh::math::CVector& position, hh::math::CQuaternion& rotation)
{
	position = hh::math::CVector(state.position[0], state.position[1], state.position[2]) * 0.01f;
	rotation = Eigen::AngleAxisf(state.faceAngle, Eigen::Vector3f::UnitY());
}

void updateMario(Sonic::Player::CPlayer* player, const hh::fnd::SUpdateInfo& updateInfo)
{
	const auto playerContext = static_cast<Sonic::Player::CPlayerSpeedContext*>(player->m_spContext.get());
	hh::math::CVector position = playerContext->m_spMatrixNode->m_Transform.m_Position;

	// Create Mario if we haven't yet, or if Sonic is dead. This needs to be delayed
	// due to the ray cast function randomly crashing on loading.
	const bool restarting = playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_PrepreRestart];

	if (restarting && mario >= 0)
	{
		sm64_mario_delete(mario);
		mario = -1;
	}

	if (mario == -1 && !restarting)
		mario = sm64_mario_create(position.x() * 100.0f, position.y() * 100.0f, position.z() * 100.0f);

	if (mario == -1)
		return;

	hh::math::CQuaternion rotation;
	hh::math::CVector velocity;

	const auto& stateName = player->m_StateMachine.GetCurrentState()->GetStateName();

	const bool controlSonic =
		playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_OutOfControl] ||
		stateName == "HangOn" ||
		strstr(stateName.c_str(), "ExternalControl") ||
		stateName == "SpecialJump" ||
		strstr(stateName.c_str(), "Homing") ||
		strstr(stateName.c_str(), "Grind") ||
		stateName == "JumpSpring" ||
		strstr(stateName.c_str(), "Rocket") ||
		stateName == "LightSpeedDash";

	DebugDrawText::log(format("State: %s", player->m_StateMachine.GetCurrentState()->GetStateName().c_str()));
	DebugDrawText::log(format("Animation: %s", playerContext->GetCurrentAnimationName().c_str()));

	float animOffset = 0.0f;

	if (controlSonic)
	{
		velocity = playerContext->m_Velocity;
		rotation = playerContext->m_spMatrixNode->m_Transform.m_Rotation;

		// Sonic's velocity is meters per second, Mario's velocity is centimeters per frame.
		velocity *= 100.0f / 30.0f;
		rotation.normalize();

		sm64_mario_set_position(mario, position.x() * 100.0f, position.y() * 100.0f, position.z() * 100.0f);
		sm64_mario_set_velocity(mario, velocity.x(), velocity.y(), velocity.z(), (rotation * hh::math::CVector::UnitZ()).dot(velocity));

		const auto direction = (rotation * hh::math::CVector::UnitZ()).normalized();

		float norm = sqrtf(direction.x() * direction.x() + direction.z() * direction.z());
		if (norm > 0.0f)
			norm = 1.0f / norm;

		const float yaw = atan2(direction.x() * norm, direction.z() * norm);
		sm64_mario_set_face_angle(mario, asin(-direction.y()), yaw, 0);

		overrideMatrix = Eigen::Translation3f(position * 100.0f) * rotation;
		useOverrideMatrix = true;

		const auto& animName = playerContext->GetCurrentAnimationName();

		int animId = -1;

		if (animName == "JumpBall" || animName == "SpinAttack")
			animId = MARIO_ANIM_FORWARD_SPINNING;

		else if (animName == "UpReelStart" || animName == "PulleyStart")
		{
			animId = MARIO_ANIM_IDLE_ON_LEDGE;
			animOffset = 1.0f;
		}

		else if (animName == "UpReelLoop" || animName == "PulleyLoop")
		{
			animId = MARIO_ANIM_IDLE_ON_LEDGE;
			animOffset = 1.0f;
		}

		else if (strstr(animName.c_str(), "HomingAttackAfter"))
			animId = MARIO_ANIM_FAST_LONGJUMP;

		else if (strstr(animName.c_str(), "GrindSide"))
			animId = MARIO_ANIM_FORWARD_SPINNING_FLIP;

		else if (strstr(animName.c_str(), "GrindQuickJump"))
			animId = MARIO_ANIM_JUMP_RIDING_SHELL;

		else if (strstr(animName.c_str(), "GrindSquat"))
			animId = MARIO_ANIM_BEND_KNESS_RIDING_SHELL;

		else if (strstr(animName.c_str(), "Grind"))
			animId = MARIO_ANIM_RIDING_SHELL;

		if (animId >= 0)
		{
			sm64_mario_set_animation_lock(mario, true);
			sm64_mario_set_animation(mario, animId);
		}
		else
			sm64_mario_set_animation_lock(mario, false);
	}

	else
	{
		computeMarioPositionAndRotation(position, rotation);

		player->m_spContext->m_spMatrixNode->m_Transform.SetRotationAndPosition(rotation, position);
		player->m_spContext->m_spMatrixNode->NotifyChanged();

		useOverrideMatrix = false;

		sm64_mario_set_animation_lock(mario, false);
	}

	// Clear sound handle flags. If Mario sets them,
	// that means the sound handle should stay.
	// See bb_play_sound above.
	memset(soundHandleFlags, 0, sizeof(soundHandleFlags));

	const auto padState = Sonic::CInputState::GetInstance()->GetPadState();
	const auto camera = Sonic::CGameDocument::GetInstance()->GetWorld()->GetCamera();

	inputs.camLookX = -camera->m_MyCamera.m_Direction.x();
	inputs.camLookZ = -camera->m_MyCamera.m_Direction.z();
	inputs.stickX = padState.LeftStickHorizontal;
	inputs.stickY = padState.LeftStickVertical;
	inputs.buttonA = padState.IsDown(Sonic::eKeyState_A);
	inputs.buttonB = padState.IsDown(Sonic::eKeyState_X);
	inputs.buttonZ = padState.IsDown(Sonic::eKeyState_LeftTrigger) || padState.IsDown(Sonic::eKeyState_RightTrigger);

	// Handle directional controls explicitly as sticks don't account for them.
	const bool left = padState.IsDown(Sonic::eKeyState_DpadLeft);
	const bool right = padState.IsDown(Sonic::eKeyState_DpadRight);
	const bool up = padState.IsDown(Sonic::eKeyState_DpadUp);
	const bool down = padState.IsDown(Sonic::eKeyState_DpadDown);

	const bool horizontal = left ^ right;
	const bool vertical = up ^ down;

	if (horizontal || vertical)
	{
		if (horizontal) inputs.stickX = left ? -1.0f : 1.0f;
		if (vertical) inputs.stickY = down ? -1.0f : 1.0f;

		const float norm = sqrtf(inputs.stickX * inputs.stickX + inputs.stickY * inputs.stickY);
		inputs.stickX /= norm;
		inputs.stickY /= norm;
	}

	if (padState.IsTapped(Sonic::eKeyState_B))
		sm64_mario_toggle_wing_cap(mario);

	sm64_mario_tick(mario, &inputs, &state, &buffers);

	if (state.update)
	{
		// Clear any sound handles that aren't persistent.
		for (size_t i = 0; i < _countof(soundHandles); i++)
		{
			if (soundHandleFlags[i]) // This sound handle should stay.
				continue;

			soundHandles[i].reset();
			soundHandleCues[i] = -1;
		}

		static bool damaged;
		const bool damaging = playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_Damaging] != 0;
		const bool dead = playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_Dead] != 0;

		if (!dead && !damaged && damaging)
			sm64_mario_take_damage(mario);

		damaged = damaging;

		sm64_mario_set_health(mario, dead ? 0 : 0x500);

		FUNCTION_PTR(void, __thiscall, changeCollision, 
			Sonic::Player::CSonicClassicContext::GetInstance() ? 0xDC30A0 : 0xDFCD20, void* This, size_t type);

		const bool attacking = sm64_mario_attacking(mario);
		changeCollision(playerContext, attacking ? 2 : 0);

		setCollision(TypeSonicBoost, attacking);
		setCollision(TypeSonicStomping, sm64_mario_diving(mario));
	}

	if (!controlSonic)
	{
		computeMarioPositionAndRotation(position, rotation);
		velocity = hh::math::CVector(state.velocity[0], state.velocity[1], state.velocity[2]) * 0.01f * 30.0f;

		playerContext->m_spMatrixNode->m_Transform.SetRotationAndPosition(rotation, position);
		playerContext->m_spMatrixNode->NotifyChanged();

		playerContext->m_HorizontalRotation = rotation;
		playerContext->m_VerticalRotation.setIdentity();

		playerContext->m_Velocity = velocity;
		playerContext->m_VelocityDirty = true;
		(&playerContext->m_VelocityDirty)[1] = false;
	}

	player->SendMessage(player->m_ActorID, boost::make_shared<Sonic::Message::MsgSetVisible>(false));

	// Update model data with the new buffers.
	// TODO: Map structures in BlueBlur to make this look cleaner.
	void* meshGroupData = **(void***)((char*)modelData.get() + 24);
	void* meshData = **(void***)((char*)meshGroupData + 16);
	auto vertexBuffer = *(DX_PATCH::IDirect3DVertexBuffer9**)((char*)meshData + 44);

	struct Vertex
	{
		float position[3];
		float color[3];
		float normal[3];
		float uv[2];
	};

	Vertex* vertices;
	vertexBuffer->Lock(0, 0, (void**)&vertices, 0);

	for (size_t i = 0; i < (size_t)buffers.numTrianglesUsed * 3; i++)
	{
		vertices[i].position[0] = (buffers.position[i * 3 + 0] - state.gfxPosition[0]) * 0.01f;
		vertices[i].position[1] = (buffers.position[i * 3 + 1] - state.gfxPosition[1]) * 0.01f;
		vertices[i].position[2] = (buffers.position[i * 3 + 2] - state.gfxPosition[2]) * 0.01f;

		memcpy(vertices[i].color, &buffers.color[i * 3], sizeof(vertices[i].color));
		memcpy(vertices[i].normal, &buffers.normal[i * 3], sizeof(vertices[i].normal));
		memcpy(vertices[i].uv, &buffers.uv[i * 2], sizeof(vertices[i].uv));
	}

	// Clear remnants
	if (previousTriangleCount > buffers.numTrianglesUsed)
		memset(&vertices[(size_t)buffers.numTrianglesUsed * 3], 0, (previousTriangleCount - buffers.numTrianglesUsed) * 3 * sizeof(Vertex));

	previousTriangleCount = buffers.numTrianglesUsed;

	vertexBuffer->Unlock();

	renderable->m_spInstanceInfo->m_Transform = Eigen::Translation3f(Eigen::Vector3f(
		state.gfxPosition[0] * 0.01f, state.gfxPosition[1] * 0.01f + animOffset, state.gfxPosition[2] * 0.01f));
}

HOOK(void, __fastcall, CGameplayFlowStageOnExit, 0xD05360, void* This)
{
	deleteMario();
	originalCGameplayFlowStageOnExit(This);
}

HOOK(void, __fastcall, CSonicUpdateParallel, 0xE17E80, Sonic::Player::CSonic* This, void* Edx, const hh::fnd::SUpdateInfo& updateInfo)
{
	originalCSonicUpdateParallel(This, Edx, updateInfo);
	updateMario(This, updateInfo);
}

HOOK(void, __fastcall, CSonicClassicUpdateParallel, 0xDDABA0, Sonic::Player::CSonic* This, void* Edx, const hh::fnd::SUpdateInfo& updateInfo)
{
	originalCSonicClassicUpdateParallel(This, Edx, updateInfo);
	updateMario(This, updateInfo);
}

HOOK(void, __fastcall, CPlayerAddCallback, 0xE799F0, Sonic::Player::CPlayer* This, void* Edx,
	const Hedgehog::Base::THolder<Sonic::CWorld>& worldHolder, Sonic::CGameDocument* pGameDocument, const boost::shared_ptr<Hedgehog::Database::CDatabase>& spDatabase)
{
	originalCPlayerAddCallback(This, Edx, worldHolder, pGameDocument, spDatabase);

	hh::mr::CMirageDatabaseWrapper mirageDatabaseWrapper(spDatabase.get());

	// Remake the "mario.dds" file using the texture generated from the ROM.
	boost::shared_ptr<hh::mr::CPictureData> spPictureData;
	mirageDatabaseWrapper.GetPictureData(spPictureData, "Mario", 0);

	FUNCTION_PTR(void, __cdecl, makePictureData, 0x743DE0, hh::mr::CPictureData* pPictureData, const uint8_t* pData, size_t length,
		hh::mr::CRenderingInfrastructure* pRenderingInfrastructure);

	if (spPictureData->m_pD3DTexture)
	{
		spPictureData->m_pD3DTexture->Release();
		spPictureData->m_pD3DTexture = nullptr;
	}

	spPictureData->m_Flags = 0;

	makePictureData(spPictureData.get(), marioTexture.get(), marioTextureSize,
		*(hh::mr::CRenderingInfrastructure**)((char*)Sonic::CApplicationDocument::GetInstance()->m_pMember + 60));

	// Add Mario's model. The vertex buffers are going to be dynamically updated using the data returned by libsm64.
	mirageDatabaseWrapper.GetModelData(modelData, "Mario", 0);
	This->AddRenderable("Object", renderable = boost::make_shared<hh::mr::CSingleElement>(modelData), true);
}

HOOK(void, __fastcall, ProcMsgSetPosition, 0xE772D0, Sonic::Player::CPlayer* This, void* Edx, Sonic::Message::MsgSetPosition& msgSetPosition)
{
	if (mario >= 0)
		sm64_mario_set_position(mario, msgSetPosition.m_Position.x() * 100.0f, msgSetPosition.m_Position.y() * 100.0f, msgSetPosition.m_Position.z() * 100.0f);

	originalProcMsgSetPosition(This, Edx, msgSetPosition);
}

constexpr double DEAD_ZONE = 0.3;

HOOK(void, __cdecl, SetSticks, 0x9C6AE0, char* data, short lowerBound, short upperBound)
{
	double x = (double)*(short*)(data + 8) / 32767.0;
	double y = (double)*(short*)(data + 10) / 32767.0;

	const double norm = sqrt(x * x + y * y);

	if (norm < DEAD_ZONE)
	{
		x = 0.0;
		y = 0.0;
	}
	else
	{
		const double newNorm = min(1, max(0, norm - DEAD_ZONE) / (1.0 - DEAD_ZONE));

		x /= norm;
		x *= newNorm;

		y /= norm;
		y *= newNorm;

		if (x > 1.0) x = 1.0;
		else if (x < -1.0) x = -1.0;

		if (y > 1.0) y = 1.0;
		else if (y < -1.0) y = -1.0;
	}

	*(float*)data = (float)x;
	*(float*)(data + 4) = (float)y;
}

void initMario()
{
	INSTALL_HOOK(CGameplayFlowStageOnExit);
	INSTALL_HOOK(CSonicUpdateParallel);
	INSTALL_HOOK(CSonicClassicUpdateParallel);
	INSTALL_HOOK(CPlayerAddCallback);
	INSTALL_HOOK(ProcMsgSetPosition);
	INSTALL_HOOK(SetSticks);

	buffers.position = new float[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.color = new float[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.normal = new float[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.uv = new float[6 * SM64_GEO_MAX_TRIANGLES];
}