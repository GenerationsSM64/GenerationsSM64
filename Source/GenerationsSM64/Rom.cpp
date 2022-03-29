#include "Mod.h"
#include "Util.h"

std::unique_ptr<uint8_t[]> rom;
size_t romSize;

struct EndianSwapInfo
{
	size_t offset;
	uint16_t count;
	uint16_t byteSize;
};

const EndianSwapInfo endianSwapInfos[] =
{
#include "EndianSwap.h"
};

void romEndianSwap()
{
	for (auto& info : endianSwapInfos)
	{
		for (size_t i = 0; i < info.count; i++)
		{
			uint8_t* data = rom.get() + info.offset + i * info.byteSize;
			switch (info.byteSize)
			{
			case 2:
				*(unsigned short*)data = _byteswap_ushort(*(unsigned short*)data);
				break;

			case 4:
				*(unsigned long*)data = _byteswap_ulong(*(unsigned long*)data);
				break;
			}
		}
	}
}

constexpr XXH128_hash_t ROM_HASH = { 0x8a90daa33e09a265, 0xc2d257a56ce0d963 };

#define MSG_HASHES "\n\nMD5: 20B854B239203BAF6C961B850A4A51A2\nSHA-1: 9BEF1128717F958171A4AFAC3ED78EE2BB4E86CE"

constexpr TCHAR MSG_TITLE[] = TEXT("GenerationsSM64");
constexpr TCHAR MSG_FIRST_BOOT[] = TEXT("This mod requires an NTSC-U ROM for Super Mario 64. Please select a valid ROM." MSG_HASHES);
constexpr TCHAR MSG_ROM_MISSING[] = TEXT("The specified Super Mario 64 ROM is missing. Please select a valid NTSC-U ROM." MSG_HASHES);
constexpr TCHAR MSG_ROM_MISMATCH[] = TEXT("The provided Super Mario 64 ROM is invalid. Please select a valid NTSC-U ROM." MSG_HASHES);

void initRom(const std::string& iniFilePath)
{
	std::string romFilePath;
	{
		const INIReader reader(iniFilePath);
		if (reader.ParseError() == 0)
			romFilePath = reader.Get("Mod", "RomFilePath", std::string());
	}

	const std::string prevRomFilePath = romFilePath;

readRom:
	rom = readAllBytes(romFilePath, romSize);
	if (!rom)
	{
		if (!romFilePath.empty())
			MessageBox(nullptr, MSG_ROM_MISSING, MSG_TITLE, MB_ICONERROR);
		else
			MessageBox(nullptr, MSG_FIRST_BOOT, MSG_TITLE, MB_ICONINFORMATION);

	promptRom:
		WCHAR fileName[1024]{};

		OPENFILENAME ofn{};
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = TEXT("NTSC-U ROM (*.z64)\0*.z64\0All Files (*.*)\0*.*\0");
		ofn.nFilterIndex = 1;
		ofn.lpstrFile = fileName;
		ofn.nMaxFile = _countof(fileName);
		ofn.lpstrTitle = TEXT("Select a valid NTSC-U ROM.");
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_EXPLORER | OFN_HIDEREADONLY | OFN_ENABLESIZING | OFN_FILEMUSTEXIST;

		if (!GetOpenFileName(&ofn))
			exit(-1);

		romFilePath.resize(_countof(fileName) + 1);
		WideCharToMultiByte(CP_UTF8, 0, fileName, -1, romFilePath.data(), _countof(fileName), nullptr, nullptr);
		romFilePath.resize(strlen(romFilePath.c_str()));

		goto readRom;
	}

	const auto romHash = XXH3_128bits(rom.get(), romSize);
	if (!XXH128_isEqual(romHash, ROM_HASH))
	{
		MessageBox(nullptr, MSG_ROM_MISMATCH, MSG_TITLE, MB_ICONERROR);
		goto promptRom;
	}

	if (prevRomFilePath != romFilePath)
	{
		std::ofstream iniStream(iniFilePath);
		iniStream << "[Mod]" << std::endl;
		iniStream << "RomFilePath=\"" << romFilePath << "\"" << std::endl;
	}

	// Endian swap the ROM.
	romEndianSwap();
}