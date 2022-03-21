#pragma once

extern std::unique_ptr<uint8_t[]> marioTexture;
extern size_t marioTextureSize;

extern SM64MarioState state;

extern void initSM64(const std::string& romFilePath);
extern void initMario();