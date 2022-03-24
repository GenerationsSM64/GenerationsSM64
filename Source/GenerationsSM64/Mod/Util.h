#pragma once

std::unique_ptr<uint8_t[]> readAllBytes(const std::string& filePath, size_t& length);