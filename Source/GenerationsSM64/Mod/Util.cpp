#include "Util.h"

std::unique_ptr<uint8_t[]> readAllBytes(const std::string& filePath, size_t& size)
{
	FILE* file = fopen(filePath.c_str(), "rb");
	if (!file)
	{
		size = 0;
		return nullptr;
	}

	fseek(file, 0, SEEK_END);

	size = ftell(file);

	std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size);
	fseek(file, 0, SEEK_SET);
	fread(data.get(), 1, size, file);

	fclose(file);

	return data;
}

void* setCollision(CollisionType collisionType, bool enabled)
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

		call[pEnableFunc]
		jmp end

	jump:
		call[pDisableFunc]

	end:
	}
}

void getPathControllerData(Sonic::CPathController* controller, hh::math::CVector* point, hh::math::CVector* invec, hh::math::CVector* outvec)
{
	static int pFunc = 0xE835F0;
	__asm
	{
		push outvec
		push point
		mov edi, invec
		mov esi, controller
		call[pFunc]
	}
}
