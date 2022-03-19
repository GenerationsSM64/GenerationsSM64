#include <libsm64/src/libsm64.h>
#include <libsm64/src/decomp/engine/surface_collision.h>

static bool fRaycast
(
	Eigen::Vector4f const& rayStartPos,
	Eigen::Vector4f const& rayEndPos,
	Eigen::Vector4f& outPos,
	Eigen::Vector4f& outNormal,
	uint32_t flag
)
{
	static void* const pfRaycast = (void*)0x10BE3B0;
	__asm
	{
		mov		ecx, rayEndPos
		push	ecx
		mov		edx, outNormal
		push	edx
		mov		eax, outPos
		push	eax
		mov		ebx, 0x1E5E2F0
		mov		ebx, [ebx]
		mov     eax, [ebx + 5ECh]
		mov     edi, [eax]
		mov		edx, flag
		mov		eax, rayStartPos
		call[pfRaycast]
	}
}

Surface surfaces[0x100];
int32_t surfaceIndex = 0;

Surface* rayCast(f32 x, f32 y, f32 z, f32* pheight, const Eigen::Vector4f& direction)
{
	Eigen::Vector4f inPos(x * 0.01f, y * 0.01f + 0.5f, z * 0.01f, 1.0f);
	Eigen::Vector4f inEndPos = inPos + direction * 1000000000.0f;
	inEndPos.w() = 1.0f;
	Eigen::Vector4f outPos, outNormal;
	if (!fRaycast(inPos, inEndPos, outPos, outNormal, 20))
		return NULL;

	*pheight = outPos.y() * 100.0f;

	Surface* surface = &surfaces[(surfaceIndex++) % _countof(surfaces)];
	memset(surface, 0, sizeof(Surface));

	surface->vertex1[0] = (outPos.x() * 100.0f);
	surface->vertex1[1] = (outPos.y() * 100.0f);
	surface->vertex1[2] = (outPos.z() * 100.0f);

	surface->vertex2[0] = (outPos.x() * 100.0f);
	surface->vertex2[1] = (outPos.y() * 100.0f);
	surface->vertex2[2] = (outPos.z() * 100.0f);

	surface->vertex3[0] = (outPos.x() * 100.0f);
	surface->vertex3[1] = (outPos.y() * 100.0f);
	surface->vertex3[2] = (outPos.z() * 100.0f);

	surface->normal.x = outNormal.x();
	surface->normal.y = outNormal.y();
	surface->normal.z = outNormal.z();

	return surface;
}

extern "C" Surface* bb_find_ceil(f32 x, f32 y, f32 z, f32 * pheight)
{
	return rayCast(x, y, z, pheight, Eigen::Vector4f::UnitY());
}

extern "C" Surface* bb_find_floor(f32 x, f32 y, f32 z, f32 * pheight)
{
	Surface* surface = rayCast(x, y, z, pheight, -Eigen::Vector4f::UnitY());
	if (!surface)
		return &surfaces[(surfaceIndex - 1) % _countof(surfaces)];
	return surface;
}

extern "C" void bb_play_sound(s32 soundBits, f32 * pos)
{
	const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();
	if (playerContext)
		playerContext->PlaySound(soundBits & 0xF0FF0000, false);
}

uint8_t ddsHeader[128] = {
	0x44, 0x44, 0x53, 0x20, 0x7C, 0x00, 0x00, 0x00, 0x0F, 0x10, 0x02, 0x00,
	0x40, 0x00, 0x00, 0x00, 0xC0, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
	0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

class MsgSetVisible : public hh::fnd::MessageTypeSet
{
public:
	HH_FND_MSG_MAKE_TYPE(0x1681F38);

	bool m_Visible;

	MsgSetVisible(bool visible) : m_Visible(visible) {}
};

std::string romFilePath;

class Mario : public Sonic::CGameObject
{
public:
	SM64Surface surfaces[1]{};
	int32_t mario{ -1 };
	SM64MarioInputs inputs{};
	SM64MarioState state{};
	SM64MarioGeometryBuffers buffers{};
	boost::shared_ptr<hh::mr::CModelData> m_spModelData;
	boost::shared_ptr<hh::mr::CSingleElement> m_spRenderable;

	~Mario() override
	{
		sm64_global_terminate();
	}

	void UpdateParallel(const hh::fnd::SUpdateInfo& updateInfo) override
	{
		const auto playerContext = Sonic::Player::CPlayerSpeedContext::GetInstance();
		if (!playerContext || !playerContext->m_spRayCastCollision_01 || !*(void**)playerContext->m_spRayCastCollision_01.get())
			return;

		if (updateInfo.DeltaTime <= 1.0 / 57.5f && updateInfo.Frame & 1)
			return;

		const auto currentPos = playerContext->m_spMatrixNode->m_Transform.m_Position;

		if (mario == -1)
			mario = sm64_mario_create(currentPos.x() * 100.0f, currentPos.y() * 100.0f, currentPos.z() * 100.0f);

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

		const auto position = hh::math::CVector(state.position[0], state.position[1], state.position[2]) * 0.01f;
		const auto rotation = Eigen::Quaternionf(Eigen::AngleAxisf(state.faceAngle, Eigen::Vector3f::UnitY()));
		const auto velocity = hh::math::CVector(state.velocity[0], state.velocity[1], state.velocity[2]) * 0.01f;

		playerContext->m_spMatrixNode->m_Transform.SetRotationAndPosition(rotation, position);
		playerContext->m_spMatrixNode->NotifyChanged();

		playerContext->m_HorizontalRotation = rotation;
		playerContext->m_VerticalRotation.setIdentity();

		playerContext->m_Velocity = velocity;
		playerContext->m_VelocityDirty = true;
		(&playerContext->m_VelocityDirty)[1] = false;

		SendMessage(playerContext->m_pPlayer->m_ActorID, boost::make_shared<MsgSetVisible>(false));

		// Update model
		void* meshGroupData = **(void***)((char*)m_spModelData.get() + 24);
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

		m_spRenderable->m_spInstanceInfo->m_Transform = Eigen::Translation3f(position);
	}

	void AddCallback(const Hedgehog::Base::THolder<Sonic::CWorld>& worldHolder,
		Sonic::CGameDocument* pGameDocument, const boost::shared_ptr<Hedgehog::Database::CDatabase>& spDatabase) override
	{
		surfaces[0].vertices[0][0] = -1;
		surfaces[0].vertices[0][2] = 1;
		surfaces[0].vertices[2][0] = -1;
		surfaces[0].vertices[2][2] = -1;
		surfaces[0].vertices[1][0] = 1;
		surfaces[0].vertices[1][2] = 1;

		buffers.position = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
		buffers.color = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
		buffers.normal = (float*)malloc(sizeof(float) * 9 * SM64_GEO_MAX_TRIANGLES);
		buffers.uv = (float*)malloc(sizeof(float) * 6 * SM64_GEO_MAX_TRIANGLES);

		FILE* file = fopen(romFilePath.c_str(), "rb");
		fseek(file, 0, SEEK_END);
		const size_t length = ftell(file);
		std::unique_ptr<uint8_t[]> rom = std::make_unique<uint8_t[]>(length);
		fseek(file, 0, SEEK_SET);
		fread(rom.get(), 1, length, file);
		fclose(file);

		constexpr size_t textureSize = sizeof(ddsHeader) + SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT * 4;
		std::unique_ptr<uint8_t[]> texture = std::make_unique<uint8_t[]>(textureSize);

		sm64_global_init(rom.get(), texture.get() + sizeof(ddsHeader), nullptr);
		sm64_static_surfaces_load(surfaces, _countof(surfaces));

		const auto applicationDocument = Sonic::CApplicationDocument::GetInstance();

		hh::mr::CMirageDatabaseWrapper mirageDatabaseWrapper(spDatabase.get());

		boost::shared_ptr<hh::mr::CPictureData> spPictureData;
		mirageDatabaseWrapper.GetPictureData(spPictureData, "mario", 0);

		memcpy(texture.get(), ddsHeader, sizeof(ddsHeader));

		FUNCTION_PTR(void, __cdecl, makePictureData, 0x743DE0, hh::mr::CPictureData * pPictureData, const uint8_t * pData, size_t length,
			hh::mr::CRenderingInfrastructure * pRenderingInfrastructure);

		if (spPictureData->m_pD3DTexture)
		{
			spPictureData->m_pD3DTexture->Release();
			spPictureData->m_pD3DTexture = nullptr;
		}
		spPictureData->m_Flags = 0;

		makePictureData(spPictureData.get(), texture.get(), textureSize, *(hh::mr::CRenderingInfrastructure**)((char*)applicationDocument->m_pMember + 60));

		pGameDocument->AddUpdateUnit("0", this);

		mirageDatabaseWrapper.GetModelData(m_spModelData, "mario", 0);
		AddRenderable("Object", m_spRenderable = boost::make_shared<hh::mr::CSingleElement>(m_spModelData), true);

		applicationDocument->AddMessageActor("GameObject", this);
	}
};

boost::shared_ptr<Mario> mario;

HOOK(void, __fastcall, ProcMsgGetPosition, 0xE769E0, Sonic::Player::CPlayer* This, void* Edx, Sonic::Message::MsgGetPosition& msgGetPosition)
{
	if (mario)
	{
		msgGetPosition.m_pPosition->x() = mario->state.position[0] * 0.01f;
		msgGetPosition.m_pPosition->y() = mario->state.position[1] * 0.01f;
		msgGetPosition.m_pPosition->z() = mario->state.position[2] * 0.01f;
	}
	else
		originalProcMsgGetPosition(This, Edx, msgGetPosition);
}

HOOK(void, __fastcall, ProcMsgGetCameraTargetPosition, 0xE69C70, Sonic::Player::CPlayer* This, void* Edx, Sonic::Message::MsgGetPosition& msgGetPosition) // nearly same structure
{
	if (mario)
	{
		msgGetPosition.m_pPosition->x() = mario->state.position[0] * 0.01f;
		msgGetPosition.m_pPosition->y() = mario->state.position[1] * 0.01f;
		msgGetPosition.m_pPosition->z() = mario->state.position[2] * 0.01f;
	}
	else
		originalProcMsgGetCameraTargetPosition(This, Edx, msgGetPosition);
}

HOOK(void, __fastcall, ProcMsgSetPosition, 0xE772D0, Sonic::Player::CPlayer* This, void* Edx, Sonic::Message::MsgSetPosition& msgSetPosition)
{
	if (mario)
		sm64_mario_set_position(mario->mario, msgSetPosition.m_Position.x() * 100.0f, msgSetPosition.m_Position.y() * 100.0f, msgSetPosition.m_Position.z() * 100.0f);

	originalProcMsgSetPosition(This, Edx, msgSetPosition);
}

void kill(void* This)
{
	static uint32_t addr = 0xD5FD10;
	__asm
	{
		mov edi, This
		call[addr]
	}
}

void killAndSetNull()
{
	kill(mario.get());
	mario = nullptr;
}

HOOK(void, __fastcall, CGameplayFlowStageOnExit, 0xD05360, void* This)
{
	if (mario)
		killAndSetNull();

	originalCGameplayFlowStageOnExit(This);
}

extern "C" void __declspec(dllexport) OnFrame()
{
	if (Sonic::Player::CPlayerSpeedContext::GetInstance() && !mario)
		Sonic::CGameDocument::GetInstance()->AddGameObject(mario = boost::make_shared<Mario>());
}

extern "C" void __declspec(dllexport) Init(ModInfo * info)
{
#if _DEBUG
	MessageBoxW(nullptr, L"Attach to Process", L"GenerationsPlayground", 0);

	if (!GetConsoleWindow())
		AllocConsole();

	freopen("CONOUT$", "w", stdout);
#endif

	std::string dir = info->CurrentMod->Path;

	size_t pos = dir.find_last_of("\\/");
	if (pos != std::string::npos)
		dir.erase(pos + 1);

	romFilePath = dir + "baserom.us.z64";

	INSTALL_HOOK(ProcMsgSetPosition);

	INSTALL_HOOK(CGameplayFlowStageOnExit);

	// Nop a good chunk of Sonic's update calls
	WRITE_MEMORY(0xE76B15, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
	WRITE_MEMORY(0xE76B21, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
	WRITE_MEMORY(0xE76B2D, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
	WRITE_MEMORY(0xE76B6F, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
	WRITE_MEMORY(0xE76B7B, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
	//WRITE_MEMORY(0xE76B87, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
	WRITE_MEMORY(0xE76B93, uint8_t, 0x83, 0xC4, 0x04, 0x90, 0x90);
}