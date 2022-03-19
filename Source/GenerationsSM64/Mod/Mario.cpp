#include "Mod.h"

extern "C" void bb_play_sound(s32 soundBits, f32* pos)
{
	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();
	if (playerContext)
		playerContext->PlaySound(soundBits & 0xF0FF0000, false);
}

int32_t mario = -1;
SM64MarioInputs inputs;
SM64MarioState state;
SM64MarioGeometryBuffers buffers;
boost::shared_ptr<hh::mr::CModelData> modelData;
boost::shared_ptr<hh::mr::CSingleElement> renderable;

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
	hh::math::CVector position = player->m_spContext->m_spMatrixNode->m_Transform.m_Position;

	// Create Mario if we haven't yet. This needs to be delayed
	// due to the ray cast function randomly crashing on loading.
	if (mario == -1)
		mario = sm64_mario_create(position.x() * 100.0f, position.y() * 100.0f, position.z() * 100.0f);

	const auto playerContext = static_cast<Sonic::Player::CPlayerSpeedContext*>(player->m_spContext.get());

	hh::math::CQuaternion rotation;
	hh::math::CVector velocity = playerContext->m_Velocity;

	// Give the control back to Sonic if out of control is enabled, otherwise, set the position and velocity.
	if (playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_OutOfControl])
	{
		sm64_mario_set_position(mario, position.x() * 100.0f, position.y() * 100.0f, position.z() * 100.0f);

		// Sonic's velocity is meters per second, Mario's velocity is centimeters per frame.
		sm64_mario_set_velocity(mario, velocity.x() * 100.0f / 30.0f, velocity.y() * 100.0f / 30.0f, velocity.z() * 100.0f / 30.0f);
	}

	else
	{
		computeMarioPositionAndRotation(position, rotation);

		player->m_spContext->m_spMatrixNode->m_Transform.SetRotationAndPosition(rotation, position);
		player->m_spContext->m_spMatrixNode->NotifyChanged();
	}

	// Update only if we're dropping frames (eg. 30 FPS) or the frame index is even.
	if (updateInfo.DeltaTime <= 1.0 / 57.5f && updateInfo.Frame & 1)
		return;
	
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

	sm64_mario_tick(mario, &inputs, &state, &buffers);

	if (!playerContext->m_pStateFlag->m_Flags[Sonic::Player::CPlayerSpeedContext::eStateFlag_OutOfControl])
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
		vertices[i].position[0] = (buffers.position[i * 3 + 0] - state.position[0]) * 0.01f;
		vertices[i].position[1] = (buffers.position[i * 3 + 1] - state.position[1]) * 0.01f;
		vertices[i].position[2] = (buffers.position[i * 3 + 2] - state.position[2]) * 0.01f;

		memcpy(vertices[i].color, &buffers.color[i * 3], sizeof(vertices[i].color));
		memcpy(vertices[i].normal, &buffers.normal[i * 3], sizeof(vertices[i].normal));
		memcpy(vertices[i].uv, &buffers.uv[i * 2], sizeof(vertices[i].uv));
	}

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