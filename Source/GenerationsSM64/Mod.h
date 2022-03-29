#pragma once

extern std::unique_ptr<uint8_t[]> rom;
extern size_t romSize;

extern std::unique_ptr<uint8_t[]> marioTexture;
extern size_t marioTextureSize;

extern SM64MarioState state;

extern bool disableWallCollision;

extern void initSM64(const std::string& romFilePath);
extern void initAudio(const std::string& directoryPath);
extern void initDatabase();
extern void initMario();