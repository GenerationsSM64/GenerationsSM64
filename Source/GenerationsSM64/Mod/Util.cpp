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
