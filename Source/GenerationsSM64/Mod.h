#pragma once

extern std::unique_ptr<uint8_t[]> rom;
extern size_t romSize;

extern std::unique_ptr<uint8_t[]> marioTexture;
extern size_t marioTextureSize;

extern SM64MarioState state;

extern bool disableWallCollision;

extern void initRom(const std::string& iniFilePath);
extern void initSM64();
extern void initAudio();
extern void initDatabase();
extern void initMario();
extern void initFPS();