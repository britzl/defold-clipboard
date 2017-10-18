#define EXTENSION_NAME clipboard
#define LIB_NAME "clipboard"
#define MODULE_NAME "clipboard"

#define DLIB_LOG_DOMAIN LIB_NAME
#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_OSX) || defined(DM_PLATFORM_WINDOWS) || defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS)

#include "clipboard_private.h"

static int copy_to_clipboard(lua_State* L) {
    const char* str = luaL_checkstring(L, 1);
    clipboard_to_clipboard(str);
    return 0;
}

static int paste_from_clipboard(lua_State* L) {
    const char* text = clipboard_from_clipboard();
    lua_pushstring(L, text);
    return 1;
}

static const luaL_reg Module_methods[] =
{
    {"copy", copy_to_clipboard},
    {"paste", paste_from_clipboard},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeClipboard(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeClipboard(dmExtension::Params* params)
{
    LuaInit(params->m_L);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeClipboard(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeClipboard(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeClipboard, AppFinalizeClipboard, InitializeClipboard, 0, 0, FinalizeClipboard)

#endif
