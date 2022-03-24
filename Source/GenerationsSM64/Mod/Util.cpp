#include "Util.h"

std::unique_ptr<uint8_t[]> readAllBytes(const std::string& filePath, size_t& length)
{
	FILE* file = fopen(filePath.c_str(), "rb");
	if (!file)
	{
		length = 0;
		return nullptr;
	}

	fseek(file, 0, SEEK_END);

	length = ftell(file);

	std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(length);
	fseek(file, 0, SEEK_SET);
	fread(data.get(), 1, length, file);

	fclose(file);

	return data;
}
