#include "Mod.h"
#include "Util.h"

hh::math::CMatrix overrideMatrix;
bool useOverrideMatrix;

extern "C" f32* geo_get_override_matrix()
{
	return useOverrideMatrix ? overrideMatrix.data() : nullptr;
}

int32_t mario = -1;
SM64MarioInputs inputs;
SM64MarioState state;
SM64MarioGeometryBuffers buffers;
boost::shared_ptr<hh::mr::CModelData> modelData;
bool useRestartIndices;
boost::shared_ptr<hh::mr::CSingleElement> renderable;
float controlSonicTimer;
bool controlSonicGrounded;
Sonic::CRigidBody* prevRigidBody;
hh::math::CMatrix prevRigidBodyMatrixInverse;
bool fireDamage;

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
	position = hh::math::CVector(state.interpolatedPosition[0], state.interpolatedPosition[1], state.interpolatedPosition[2]) * 0.01f;
	rotation = Eigen::AngleAxisf(state.interpolatedFaceAngle, Eigen::Vector3f::UnitY());
}

void updateMarioMesh(void* meshData, const SM64MarioGeometryBuffer& buffer)
{
	if (buffer.numTrianglesUsed == 0)
	{
		// Set index count to 0.
		*(size_t*)((char*)meshData + 12) = 0;
		return;
	}

	auto vertexBuffer = *(DX_PATCH::IDirect3DVertexBuffer9**)((char*)meshData + 44);
	const size_t vertexCount = (size_t)buffer.numTrianglesUsed * 3;

	struct Vertex
	{
		float position[3];
		float color[3];
		float normal[3];
		float uv[2];
	};

	Vertex* vertices;
	vertexBuffer->Lock(0, vertexCount * sizeof(Vertex), (void**)&vertices, 0);

	for (size_t i = 0; i < vertexCount; i++)
	{
		vertices[i].position[0] = (buffer.position[i * 3 + 0] - state.interpolatedGfxPosition[0]) * 0.01f;
		vertices[i].position[1] = (buffer.position[i * 3 + 1] - state.interpolatedGfxPosition[1]) * 0.01f;
		vertices[i].position[2] = (buffer.position[i * 3 + 2] - state.interpolatedGfxPosition[2]) * 0.01f;

		memcpy(vertices[i].color, &buffer.color[i * 3], sizeof(vertices[i].color));
		memcpy(vertices[i].normal, &buffer.normal[i * 3], sizeof(vertices[i].normal));
		memcpy(vertices[i].uv, &buffer.uv[i * 2], sizeof(vertices[i].uv));
	}

	vertexBuffer->Unlock();

	// Set index count.
	// D3D9 has 6 indices per triangle, last 3 being the degenerate triangle.
	// D3D11 has 4 indices per triangle, last one being the restart index.
	*(size_t*)((char*)meshData + 12) = useRestartIndices ? vertexCount + buffer.numTrianglesUsed - 1 : vertexCount * 2 - 3;
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

	if (!sm64_mario_bind(mario))
		return;

	hh::math::CQuaternion rotation;
	hh::math::CVector velocity;

	const auto& stateName = player->m_StateMachine.GetCurrentState()->GetStateName();

	const bool invulnerable = sm64_mario_is_invulnerable();

	bool ignoreInput = 
		(!invulnerable && playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_OutOfControl]) ||
		stateName == "HangOn" ||
		stateName == "LightSpeedDash" ||
		stateName == "TramRiding" ||
		stateName == "Stepping" ||
		strstr(stateName.c_str(), "ExternalControl") ||
		strstr(stateName.c_str(), "Grind") ||
		strstr(stateName.c_str(), "Homing") ||
		strstr(stateName.c_str(), "Pipe") ||
		strstr(stateName.c_str(), "Rocket") ||
		strstr(stateName.c_str(), "Spike");

	bool controlSonic =
		ignoreInput ||
		stateName == "JumpSpring" ||
		stateName == "SpecialJump";

	if (controlSonic)
	{
		if (controlSonicTimer <= 0.0f)
			controlSonicGrounded = playerContext->m_Grounded;

		// Don't let grind states allow extra control.
		controlSonicTimer = strstr(stateName.c_str(), "Grind") ? 0.0f : max(0.5f, 
			min(1.0f, playerContext->m_Velocity.norm() / 100.0f));
	}

	// Allow Sonic to be in control for a little longer.
	else if (controlSonicTimer > 0.0f)
	{
		// If Sonic is pointing down after losing control (eg. in the middle of a loop),
		// let him control for a little longer, otherwise Mario is going to clip through the ceiling.
		if (playerContext->m_UpVector.y() <= 0.01f)
		{
			controlSonic = true;
			controlSonicTimer = max(0.25f, controlSonicTimer);
		}

		// Abort if grounded flag doesn't match, or we're invulnerable.
		else if (playerContext->m_Grounded != controlSonicGrounded || invulnerable)
			controlSonicTimer = 0.0f;

		else
		{
			controlSonic = true;
			controlSonicTimer -= updateInfo.DeltaTime;
		}
	}

	DebugDrawText::log(format("%d", controlSonic));

	disableWallCollision = controlSonic;
	sm64_mario_set_external_control(controlSonic);

	DebugDrawText::log(format("State: %s", player->m_StateMachine.GetCurrentState()->GetStateName().c_str()));
	DebugDrawText::log(format("Animation: %s", playerContext->GetCurrentAnimationName().c_str()));

	float animOffset = 0.0f;
	float scale = 1.0f;

	if (controlSonic)
	{
		velocity = playerContext->m_Velocity;
		rotation = playerContext->m_spMatrixNode->m_Transform.m_Rotation;

		// Sonic's velocity is meters per second, Mario's velocity is centimeters per frame.
		velocity *= 100.0f / 30.0f;
		rotation.normalize();

		// Offset Mario's position if pointing down to prevent clipping when external control ends.
		auto offsetedPosition = position;
		if (playerContext->m_UpVector.y() <= 0.01f)
			offsetedPosition += playerContext->m_UpVector * 0.6f;

		sm64_mario_set_position(offsetedPosition.x() * 100.0f, offsetedPosition.y() * 100.0f, offsetedPosition.z() * 100.0f, TRUE);
		sm64_mario_set_velocity(velocity.x(), velocity.y(), velocity.z(), (rotation * hh::math::CVector::UnitZ()).dot(velocity));

		hh::math::CVector direction;
		if (playerContext->m_HorizontalVelocity.squaredNorm() > 0.001f)
			direction = playerContext->m_HorizontalVelocity.normalized();
		else
			direction = (rotation * hh::math::CVector::UnitZ()).normalized();

		const float yaw = atan2(direction.x(), direction.z());
		sm64_mario_set_face_angle(asin(-direction.y()), yaw, 0);

		overrideMatrix = Eigen::Translation3f(position * 100.0f) * rotation;
		useOverrideMatrix = true;

		const auto& animName = playerContext->GetCurrentAnimationName();

		int animId = -1;

		if (animName == "JumpBall" || animName == "SpinAttack")
		{
			animId = MARIO_ANIM_FORWARD_SPINNING;

			if (stateName == "TransformRocket" || stateName == "TransformSpike")
				scale = 0.0f;
		}

		else if (animName == "UpReelStart" || animName == "PulleyStart" ||
			animName == "UpReelLoop" || animName == "PulleyLoop" ||
			strstr(animName.c_str(), "CatchRocket"))
		{
			animId = MARIO_ANIM_IDLE_ON_LEDGE;
			animOffset = 1.1f;
		}

		else if (animName == "TransformRocket" || animName == "TransformSpike")
		{
			animId = MARIO_ANIM_FORWARD_SPINNING;

			if (stateName != "TransformRocket" && stateName != "TransformSpike")
				scale = 0.0f;
		}

		else if (animName == "Squat")
			animId = MARIO_ANIM_CROUCHING;

		else if (animName == "LightSpeedDash")
			animId = MARIO_ANIM_WING_CAP_FLY;

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

		else if (strstr(animName.c_str(), "Result") && strstr(animName.c_str(), "_Link"))
			animId = MARIO_ANIM_STAR_DANCE;

		else if (strstr(animName.c_str(), "HangPole") || strstr(animName.c_str(), "CatchHeri"))
		{
			if (animName == "HangPoleB")
				animId = MARIO_ANIM_MOVE_ON_WIRE_NET_RIGHT;
			else if (animName == "HangPoleF")
				animId = MARIO_ANIM_MOVE_ON_WIRE_NET_LEFT;
			else
				animId = MARIO_ANIM_HANG_ON_CEILING;

			animOffset = -0.25f;
		}

		if (animId >= 0)
		{
			sm64_mario_set_animation_lock(TRUE);
			sm64_mario_set_animation(animId);
		}
		else
			sm64_mario_set_animation_lock(FALSE);

		int action = -1;

		if (animName == "Walk")
			action = ACT_WALKING;

		else if (animName == "Stand" || stateName == "Goal" || animName == "StartEventDash" || animName == "StartEventStand")
			action = ACT_IDLE;

		else if (!playerContext->m_Grounded)
			action = ACT_FREEFALL;

		if (action >= 0)
			sm64_mario_set_action(action);
	}

	else
	{
		computeMarioPositionAndRotation(position, rotation);

		player->m_spContext->m_spMatrixNode->m_Transform.SetRotationAndPosition(rotation, position);
		player->m_spContext->m_spMatrixNode->NotifyChanged();

		useOverrideMatrix = false;

		sm64_mario_set_animation_lock(FALSE);
	}

	const auto& padState = Sonic::CInputState::GetInstance()->GetPadState();
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

	if (ignoreInput)
	{
		inputs.stickX = 0.0f;
		inputs.stickY = 0.0f;
		inputs.buttonA = 0;
		inputs.buttonB = 0;
		inputs.buttonZ = 0;
	}
	else if (padState.IsTapped(Sonic::eKeyState_B))
		sm64_mario_toggle_wing_cap();

	const auto viewPosition = camera->m_MyCamera.m_View * position.head<3>();
	sm64_mario_set_camera_to_object(viewPosition.x() * 100.0f, viewPosition.y() * 100.0f, viewPosition.z() * 100.0f);

	sm64_mario_tick(&inputs, &state, &buffers);

	if (state.isUpdateFrame)
	{
		hh::math::CVector pos;
		pos.x() = state.position[0] * 0.01f;
		pos.y() = state.position[1] * 0.01f;
		pos.z() = state.position[2] * 0.01f;

		Sonic::CRigidBody* prevRigidBody = ::prevRigidBody;
		::prevRigidBody = nullptr;

		if (!controlSonic && !sm64_mario_is_airborne())
		{
			// Detect if we are on a moving rigid body.
			// Convert Mario's position to rigid body's local space using the previous frame,
			// then convert it back to world space using the current matrix.
			RayCastQuery query;
			if (rayCastPlayerTerrain(playerContext, query,
				pos + hh::math::CVector(0, +0.5f, 0),
				pos + hh::math::CVector(0, -0.5f, 0)) && query.rigidBody)
			{
				const auto hkpRigidBody = *(char**)((char*)query.rigidBody + 4);

				if (hkpRigidBody)
				{
					const auto& rigidBodyMatrix =
						*(hh::math::CMatrix*)(hkpRigidBody + 0xF0); // accesses hkpRigidBody?

					if (prevRigidBody == query.rigidBody)
						pos = rigidBodyMatrix * (prevRigidBodyMatrixInverse * pos.head<3>());

					::prevRigidBody = query.rigidBody;
					prevRigidBodyMatrixInverse = rigidBodyMatrix.inverse();
				}
			}
		}

		// Let the player to go OOB when bumpers are held.
		if (!padState.IsDown(Sonic::eKeyState_LeftBumper) && !padState.IsDown(Sonic::eKeyState_RightBumper) &&
			playerContext->m_Is2DMode && playerContext->m_sp2DPathController_01)
		{
			hh::math::CVector point, upVec, leftVec;
			getPathControllerData(playerContext->m_sp2DPathController_01.get(), &point, &upVec, &leftVec);

			const auto frontVec = upVec.cross(leftVec).normalized();
			pos -= (pos - point).dot(frontVec) * frontVec * 0.8f;
		}

		sm64_mario_set_position(pos.x() * 100.0f, pos.y() * 100.0f, pos.z() * 100.0f, FALSE);
	}

	sm64_mario_post_tick(&state);

	if (state.isUpdateFrame)
	{
		static bool damaged;

		const bool damaging = strstr(stateName.c_str(), "Damage") != nullptr;
		const bool dead = playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_Dead] != 0;

		if (!dead && !damaged && damaging)
		{
			if (stateName == "DamageShock")
				sm64_mario_take_shock_damage();

			else if (fireDamage)
				sm64_mario_take_fire_damage();

			else
			{
				sm64_mario_take_damage();

				if (stateName == "PressDamage")
					sm64_mario_squish();
			}
		}

		damaged = damaging;

		sm64_mario_set_health(dead ? 0 : 0x500);

		FUNCTION_PTR(void, __thiscall, changeCollision,
			Sonic::Player::CSonicClassicContext::GetInstance() ? 0xDC30A0 : 0xDFCD20, void* This, size_t type);

		const bool boost = sm64_mario_should_use_boost_collision();
		const bool stomp = sm64_mario_should_use_stomp_collision();
		const bool squat = sm64_mario_should_use_squat_collision();

		changeCollision(playerContext, boost ? 2 : squat ? 1 : 0);
		setCollision(TypeSonicBoost, boost);
		setCollision(TypeSonicStomping, stomp);

		playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_Boost] = boost;
		playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_Squat] = squat;
	}

	if (!controlSonic)
	{
		computeMarioPositionAndRotation(position, rotation);
		velocity = hh::math::CVector(state.interpolatedVelocity[0], state.interpolatedVelocity[1], state.interpolatedVelocity[2]) * 0.01f * 30.0f;

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
	updateMarioMesh(**(void***)((char*)meshGroupData + 16), buffers.opaque);
	updateMarioMesh(**(void***)((char*)meshGroupData + 48), buffers.punchThrough);

	renderable->m_spInstanceInfo->m_Transform = Eigen::Translation3f(Eigen::Vector3f(
		state.interpolatedGfxPosition[0] * 0.01f, state.interpolatedGfxPosition[1] * 0.01f + animOffset, state.interpolatedGfxPosition[2] * 0.01f));

	if (!playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_Damaging] && *((bool*)playerContext + 0x112C))
		scale = 0.0f;

	renderable->m_spInstanceInfo->m_Transform *= Eigen::Scaling(scale); 
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

	// Remake the "Dynamic.dds" file using the texture generated from the ROM.
	boost::shared_ptr<hh::mr::CPictureData> spPictureData;
	mirageDatabaseWrapper.GetPictureData(spPictureData, "Dynamic", 0);

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

	// Add the dynamic model. The vertex buffers are going to be dynamically updated using the data returned by libsm64.
	mirageDatabaseWrapper.GetModelData(modelData, "Dynamic", 0);
	This->AddRenderable("Object", renderable = boost::make_shared<hh::mr::CSingleElement>(modelData), true);
}

HOOK(void, __fastcall, ProcMsgSetPosition, 0xE772D0, Sonic::Player::CPlayer* This, void* Edx, Sonic::Message::MsgSetPosition& msgSetPosition)
{
	if (mario >= 0)
		sm64_mario_set_position(msgSetPosition.m_Position.x() * 100.0f, msgSetPosition.m_Position.y() * 100.0f, msgSetPosition.m_Position.z() * 100.0f, TRUE);

	originalProcMsgSetPosition(This, Edx, msgSetPosition);
}

HOOK(void, __fastcall, ProcMsgRestartStage, 0xE76810, Sonic::Player::CPlayer* This, void* Edx, hh::fnd::Message& message)
{
	if (mario >= 0)
	{
		sm64_mario_delete(mario);
		mario = -1;
	}
	originalProcMsgRestartStage(This, Edx, message);
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

// The game is unable to detect box collision
// due to being pushed too far away from it.

// Due to this, we should do the "pushing" by
// checking whether Mario is pushing a wall.
void __declspec(naked) pushBoxMidAsmHook()
{
	static uint32_t pushBoxAddrOnFalse = 0xE58CB0;
	static uint32_t pushBoxAddrOnTrue = 0xE588AA;

	__asm
	{
		call sm64_mario_pushing_wall
		cmp al, 0
		jnz onTrue
		jmp [pushBoxAddrOnFalse]

	onTrue:
		jmp [pushBoxAddrOnTrue]
	}
}

HOOK(void, __stdcall, StartOutOfControl, 0xE5AC00, Sonic::Player::CPlayerContext* playerContext, float time)
{
	return originalStartOutOfControl(playerContext, time >= 0 ? max(0.05f, time) : min(-0.05f, time));
}

HOOK(void, __fastcall, CPlayerSpeedStatePluginDamageFireEnter, 0x1233740, void* This)
{
	fireDamage = true;
	originalCPlayerSpeedStatePluginDamageFireEnter(This);
}

HOOK(void, __fastcall, CPlayerSpeedStatePluginDamageFireLeave, 0x12335D0, void* This)
{
	fireDamage = false;
	originalCPlayerSpeedStatePluginDamageFireLeave(This);
}

void initMario()
{
	INSTALL_HOOK(CGameplayFlowStageOnExit);
	INSTALL_HOOK(CSonicUpdateParallel);
	INSTALL_HOOK(CSonicClassicUpdateParallel);
	INSTALL_HOOK(CPlayerAddCallback);
	INSTALL_HOOK(ProcMsgSetPosition);
	INSTALL_HOOK(ProcMsgRestartStage);
	INSTALL_HOOK(SetSticks);
	WRITE_JUMP(0xE58899, pushBoxMidAsmHook);
	INSTALL_HOOK(StartOutOfControl);
	INSTALL_HOOK(CPlayerSpeedStatePluginDamageFireEnter);
	INSTALL_HOOK(CPlayerSpeedStatePluginDamageFireLeave);

	// Allocate a continuous vertex buffer and give parts of it to vertex elements.
	const auto bufferHeap = new float[(3 + 3 + 3 + 2) * 3 * SM64_GEO_MAX_TRIANGLES * 2];

	buffers.opaque.position = &bufferHeap[0];
	buffers.opaque.normal = &buffers.opaque.position[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.opaque.color = &buffers.opaque.normal[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.opaque.uv = &buffers.opaque.color[9 * SM64_GEO_MAX_TRIANGLES];

	buffers.punchThrough.position = &buffers.opaque.uv[6 * SM64_GEO_MAX_TRIANGLES];
	buffers.punchThrough.normal = &buffers.punchThrough.position[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.punchThrough.color = &buffers.punchThrough.normal[9 * SM64_GEO_MAX_TRIANGLES];
	buffers.punchThrough.uv = &buffers.punchThrough.color[9 * SM64_GEO_MAX_TRIANGLES];
}

extern "C" __declspec(dllexport) void PostInit()
{
	// D3D11 mod uses restart indices for index data.
	useRestartIndices = GetModuleHandle(TEXT("GenerationsD3D11.dll")) != nullptr;
}