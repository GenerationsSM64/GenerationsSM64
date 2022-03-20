#include "Mod.h"

#define DEBUG_DRAW_TEXT_DLL_IMPORT
#include "../../GenerationsParameterEditor/Include/DebugDrawText.h"

extern "C" void bb_play_sound(s32 soundBits, f32* pos)
{
	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();
	if (playerContext)
		playerContext->PlaySound(soundBits & 0xF0FF0000, false);
}

hh::math::CMatrix customTransform;
bool useCustomTransform;

extern "C" f32* bb_get_custom_mario_transform()
{
	return useCustomTransform ? customTransform.data() : nullptr;
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
		strstr(stateName.c_str(), "Grind");

	DebugDrawText::log(format("State: %s", player->m_StateMachine.GetCurrentState()->GetStateName().c_str()));

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

		customTransform = Eigen::Translation3f(position * 100.0f) * rotation;
		useCustomTransform = true;

		const auto& animName = playerContext->GetCurrentAnimationName();

		DebugDrawText::log(format("Animation: %s", animName.c_str()));

		int animId = -1;

		if (animName == "JumpBall" || animName == "SpinAttack")
			animId = MARIO_ANIM_FORWARD_SPINNING;

		else if (animName == "UpReelStart")
		{
			animId = MARIO_ANIM_FAST_LEDGE_GRAB;
			animOffset = 1.0f;
		}

		else if (animName == "UpReelLoop")
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

		useCustomTransform = false;

		sm64_mario_set_animation_lock(mario, false);
	}

	const auto padState = Sonic::CInputState::GetInstance()->GetPadState();
	const auto camera = Sonic::CGameDocument::GetInstance()->GetWorld()->GetCamera();

	auto direction = camera->m_MyCamera.m_Direction;
	direction.y() = 0.0f;
	direction.normalize();

	inputs.camLookX = direction.x();
	inputs.camLookZ = direction.z();
	inputs.stickX = padState.LeftStickHorizontal;
	inputs.stickY = -padState.LeftStickVertical;
	inputs.buttonA = padState.IsDown(Sonic::eKeyState_A) || padState.IsDown(Sonic::eKeyState_B);
	inputs.buttonB = padState.IsDown(Sonic::eKeyState_X) || padState.IsDown(Sonic::eKeyState_Y);
	inputs.buttonZ = padState.IsDown(Sonic::eKeyState_LeftTrigger) || padState.IsDown(Sonic::eKeyState_RightTrigger);

	const bool update = updateInfo.DeltaTime >= 1.0f / 45.0f || (updateInfo.Frame & 1) == 0;

	if (update)
		sm64_mario_tick(mario, &inputs, &state, &buffers);

	sm64_mario_set_health(mario, 0x500); // For now.

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
		vertices[i].position[0] = buffers.position[i * 3 + 0] * 0.01f - position.x();
		vertices[i].position[1] = buffers.position[i * 3 + 1] * 0.01f - position.y() + animOffset;
		vertices[i].position[2] = buffers.position[i * 3 + 2] * 0.01f - position.z();

		memcpy(vertices[i].color, &buffers.color[i * 3], sizeof(vertices[i].color));
		memcpy(vertices[i].normal, &buffers.normal[i * 3], sizeof(vertices[i].normal));
		memcpy(vertices[i].uv, &buffers.uv[i * 2], sizeof(vertices[i].uv));
	}

	// Clear remnants
	if (previousTriangleCount > buffers.numTrianglesUsed)
		memset(&vertices[(size_t)buffers.numTrianglesUsed * 3], 0, (previousTriangleCount - buffers.numTrianglesUsed) * 3 * sizeof(Vertex));

	previousTriangleCount = buffers.numTrianglesUsed;

	vertexBuffer->Unlock();

	renderable->m_spInstanceInfo->m_Transform = Eigen::Translation3f(position);
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
	mirageDatabaseWrapper.GetPictureData(spPictureData, "mario", 0);

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
	mirageDatabaseWrapper.GetModelData(modelData, "mario", 0);
	This->AddRenderable("Object", renderable = boost::make_shared<hh::mr::CSingleElement>(modelData), true);
}

HOOK(void, __fastcall, ProcMsgSetPosition, 0xE772D0, Sonic::Player::CPlayer* This, void* Edx, Sonic::Message::MsgSetPosition& msgSetPosition)
{
	if (mario >= 0)
		sm64_mario_set_position(mario, msgSetPosition.m_Position.x() * 100.0f, msgSetPosition.m_Position.y() * 100.0f, msgSetPosition.m_Position.z() * 100.0f);

	originalProcMsgSetPosition(This, Edx, msgSetPosition);
}

void initMario()
{
	INSTALL_HOOK(CGameplayFlowStageOnExit);
	INSTALL_HOOK(CSonicUpdateParallel);
	INSTALL_HOOK(CSonicClassicUpdateParallel);
	INSTALL_HOOK(CPlayerAddCallback);
	INSTALL_HOOK(ProcMsgSetPosition);

	buffers.position = new float[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.color = new float[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.normal = new float[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.uv = new float[6 * SM64_GEO_MAX_TRIANGLES];
}