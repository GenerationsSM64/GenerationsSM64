#include "Mod.h"

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

	initRom(dir + "GenerationsSM64.ini");
	initSM64();
	initAudio();
	initDatabase();
	initMario();
	initFPS();
	initCollision();
}