﻿#include "Mod.h"

FUNCTION_PTR(void*, __thiscall, fun69C270, 0x69C270, 
    void* This, const boost::shared_ptr<void>& a2, const hh::base::CSharedString& arFileName, const hh::base::CSharedString& arlFileName, void* a5);

FUNCTION_PTR(void*, __thiscall, fun69AFF0, 0x69AFF0, 
    void* This, boost::shared_ptr<void> a2, const hh::base::CSharedString& arlFileName);

FUNCTION_PTR(void*, __thiscall, fun69AB10, 0x69AB10,
    void* This, boost::shared_ptr<void> a3, const hh::base::CSharedString& arFileName, void* a6, uint32_t a7, uint32_t a8);

FUNCTION_PTR(void*, __thiscall, fun446F90, 0x446F90, void* This, uint32_t a2, uint32_t a3);
FUNCTION_PTR(void*, __thiscall, fun446E30, 0x446E30, void* This);

HOOK(void*, __stdcall, LoadApplicationAndShaders, 0xD6A580, void* This)
{
    void* archiveDatabaseLoader = *(void**)(*(uint32_t*)((uint32_t)This + 4) + 200);

    uint32_t unk0[53];

    fun446F90((void*)&unk0, 200, 5);
    fun69C270(archiveDatabaseLoader, boost::shared_ptr<void>(), "VertexColorFadeToInvAlpha.ar", "VertexColorFadeToInvAlpha.arl", (void*)&unk0);
    fun446E30((void*)&unk0);

    uint32_t field04 = *(uint32_t*)((uint32_t)This + 4);
    boost::shared_ptr<void> field88 = *(boost::shared_ptr<void>*)(field04 + 136);

    fun69AFF0(archiveDatabaseLoader, field88, "VertexColorFadeToInvAlpha.arl");

    fun446F90((void*)&unk0, -10, 5);
    fun69AB10(archiveDatabaseLoader, field88, "VertexColorFadeToInvAlpha.ar", (void*)&unk0, 0, 0);
    fun446E30((void*)&unk0);

    return originalLoadApplicationAndShaders(This);
}

constexpr std::string_view ARCHIVE_TREE_DATA =
"  <DefAppend>\n"
"    <Name>SonicActionCommon</Name>\n"
"    <Archive>DynamicData</Archive>\n"
"  </DefAppend>\n";

HOOK(bool, __stdcall, ParseArchiveTree, 0xD4C8E0, void* A1, char* data, const size_t size, void* database)
{
    const size_t newSize = size + ARCHIVE_TREE_DATA.size();
    const std::unique_ptr<char[]> buffer = std::make_unique<char[]>(newSize);
    memcpy(buffer.get(), data, size);

    char* insertionPos = strstr(buffer.get(), "<Include>");

    memmove(insertionPos + ARCHIVE_TREE_DATA.size(), insertionPos, size - (size_t)(insertionPos - buffer.get()));
    memcpy(insertionPos, ARCHIVE_TREE_DATA.data(), ARCHIVE_TREE_DATA.size());

    bool result;
    {
        result = originalParseArchiveTree(A1, buffer.get(), newSize, database);
    }

    return result;
}

void initDatabase()
{
    INSTALL_HOOK(LoadApplicationAndShaders);
    INSTALL_HOOK(ParseArchiveTree);
}