/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

// Free list of references
#define FREELIST_REF    0       

// Convert a stack index to positive
#define abs_index(L, i) \
    ((i) > 0 || (i) <= LUA_REGISTRYINDEX ? (i) : lua_gettop(L) + (i) + 1)

static void* DefaultAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    if (nsize == 0)
    {
        free(ptr);
        return 0;
    }
    else
    {
        return realloc(ptr, nsize);
    }
}

struct LoadString
{
    const char* buffer;
    size_t      size;
};

struct LoadFile
{
    FILE*       file;
    char        buffer[LUAL_BUFFERSIZE];
};

static const char* ReadString(lua_State* L, void* ud, size_t *size)
{
    LoadString* ls = static_cast<LoadString*>(ud);
    if (ls->size == 0)
    {
        return NULL;
    }
    *size = ls->size;
    ls->size = 0;
    return ls->buffer;
}

static const char* ReadFile(lua_State *L, void *ud, size_t *size)
{
    LoadFile* lf = static_cast<LoadFile*>(ud);
    if (feof(lf->file))
    {
        return NULL;
    }
    *size = fread(lf->buffer, 1, sizeof(lf->buffer), lf->file);
    if (*size == 0)
    {
        return NULL;
    }
    return lf->buffer;
}

lua_State* luaL_newstate()
{
    return lua_newstate(DefaultAlloc, 0);
}

void luaL_register(lua_State* L, const char* libname, const luaL_Reg* l)
{
    if (libname != NULL)
    {
        // See if a table with the specified name already exists.
        lua_getglobal(L, libname);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setglobal(L, libname);
        }
        // TODO: Set package.loaded[libname] = t
    }
    for (int i = 0; l[i].name != NULL; ++i)
    {
        lua_pushcfunction(L, l[i].func);
        lua_setfield(L, -2, l[i].name);
    }
}

LUALIB_API int luaL_argerror(lua_State *L, int narg, const char *extramsg)
{
    // TODO complete this.
    return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
}

LUALIB_API int luaL_typerror (lua_State *L, int narg, const char *tname)
{
    const char* msg = lua_pushfstring(L, "%s expected, got %s", tname, luaL_typename(L, narg));
    return luaL_argerror(L, narg, msg);
}

LUALIB_API void luaL_where (lua_State* L, int level)
{
    lua_Debug ar;
    if (lua_getstack(L, level, &ar))
    {  
        /* check function at level */
        lua_getinfo(L, "Sl", &ar);  /* get info about it */
        if (ar.currentline > 0)
        {  
            /* is there info? */
            lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
            return;
        }
    }
    lua_pushliteral(L, "");  /* else, no information available... */
}

LUALIB_API int luaL_error(lua_State* L, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    luaL_where(L, 1);
    lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    lua_concat(L, 2);
    return lua_error(L);
}

LUALIB_API lua_Integer luaL_checkinteger(lua_State *L, int narg)
{
    lua_Integer d = lua_tointeger(L, narg);
    //  Avoid extra test when d is not 0
    if (d == 0 && !lua_isnumber(L, narg))
    {
        luaL_error(L, "expected number for arg %d", narg);
    }
    return d;
}

LUALIB_API void luaL_openlibs(lua_State *L)
{

    static const luaL_Reg libs[] =
        {
            {"", luaopen_base},
            //{LUA_LOADLIBNAME, luaopen_package},
            //{LUA_TABLIBNAME, luaopen_table},
            {LUA_IOLIBNAME, luaopen_io},
            {LUA_OSLIBNAME, luaopen_os},
            {LUA_STRLIBNAME, luaopen_string},
            {LUA_MATHLIBNAME, luaopen_math},
            //{LUA_DBLIBNAME, luaopen_debug},
            {NULL, NULL}
        };

    for (const luaL_Reg* lib = libs; lib->func; ++lib)
    {
        lua_pushcfunction(L, lib->func);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }

}

LUALIB_API void luaL_checkany (lua_State *L, int narg)
{
    if (lua_type(L, narg) == LUA_TNONE)
    {
        luaL_argerror(L, narg, "value expected");
    }
}

LUALIB_API const char* luaL_checklstring(lua_State *L, int narg, size_t* length)
{
    const char* string = lua_tolstring(L, narg, length);
    if (string == NULL)
    {
        luaL_argerror(L, narg, "string expected");
    }
    return string;
}

LUALIB_API const char* luaL_checkstring(lua_State* L, int n)
{
    return luaL_checklstring(L, n, NULL);
}

LUALIB_API lua_Number luaL_checknumber(lua_State* L, int narg)
{
    lua_Number d = lua_tonumber(L, narg);
    // Avoid extra test when d is not 0.
    if (d == 0.0 && !lua_isnumber(L, narg))
    {
        luaL_argerror(L, narg, "number expected");
    }
    return d;
}

LUALIB_API lua_Integer luaL_optinteger (lua_State *L, int narg, lua_Integer def)
{
    return luaL_opt(L, luaL_checkinteger, narg, def);
}

LUALIB_API int luaL_checkint(lua_State* L, int n)
{
    return static_cast<int>( luaL_checkinteger(L, n) );
}

LUALIB_API int luaL_optint(lua_State* L, int n, int d)
{
    return static_cast<int>( luaL_optinteger(L, n, d) );
}

LUALIB_API const char* luaL_typename(lua_State* L, int index)
{
    return lua_typename( L, lua_type(L, index) );
}

LUALIB_API void luaL_checktype (lua_State *L, int narg, int type)
{
    if (lua_type(L, narg) != type)
    {
        luaL_typerror(L, narg, lua_typename(L, type));
    }
}

LUALIB_API int luaL_loadfile (lua_State *L, const char* fileName)
{
    LoadFile lf;
    int fnameindex = lua_gettop(L) + 1;  // index of filename on the stack
    if (fileName == NULL)
    {
        lf.file = stdin;
        lua_pushliteral(L, "=stdin");
    }
    else
    {
        lf.file = fopen(fileName, "r");
        if (lf.file == NULL)
        {
            lua_pushfstring(L, "cannot open %s", fileName);
            return LUA_ERRFILE;
        }
        lua_pushfstring(L, "@%s", fileName);
    }
    int status = lua_load(L, ReadFile, &lf, lua_tostring(L, -1));
    int readstatus = ferror(lf.file);
    if (fileName)
    {
        fclose(lf.file);
    }
    if (readstatus)
    {
        // Ignore results from lua_load
        lua_settop(L, fnameindex);
        return LUA_ERRFILE;
    }
    lua_remove(L, fnameindex);
    return status;
}

LUALIB_API int luaL_loadbuffer(lua_State *L, const char* buffer, size_t size, const char *name)
{
    LoadString ls;
    ls.buffer = buffer;
    ls.size   = size;
    return lua_load(L, ReadString, &ls, name);
}

LUALIB_API int luaL_loadstring(lua_State* L, const char* string)
{
    return luaL_loadbuffer(L, string, strlen(string), string);
}

LUALIB_API int luaL_getmetafield (lua_State *L, int obj, const char* event)
{
    // No metatable?
    if (!lua_getmetatable(L, obj))
    {
        return 0;
    }
    lua_pushstring(L, event);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1))
    {
        // Remove metatable and metafield.
        lua_pop(L, 2);
        return 0;
    }
    // Remove only metatable.
    lua_remove(L, -2);
    return 1;
}

LUALIB_API int luaL_ref (lua_State *L, int t)
{
  int ref;
  t = abs_index(L, t);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* remove from stack */
    return LUA_REFNIL;  /* `nil' has a unique fixed reference */
  }
  lua_rawgeti(L, t, FREELIST_REF);  /* get first free element */
  ref = (int)lua_tointeger(L, -1);  /* ref = t[FREELIST_REF] */
  lua_pop(L, 1);  /* remove it from stack */
  if (ref != 0) {  /* any free element? */
    lua_rawgeti(L, t, ref);  /* remove it from list */
    lua_rawseti(L, t, FREELIST_REF);  /* (t[FREELIST_REF] = t[ref]) */
  }
  else {  /* no free elements */
    ref = (int)lua_objlen(L, t);
    ref++;  /* create new reference */
  }
  lua_rawseti(L, t, ref);
  return ref;
}


LUALIB_API void luaL_unref (lua_State *L, int t, int ref)
{
  if (ref >= 0)
  {
    t = abs_index(L, t);
    lua_rawgeti(L, t, FREELIST_REF);
    lua_rawseti(L, t, ref);  /* t[ref] = t[FREELIST_REF] */
    lua_pushinteger(L, ref);
    lua_rawseti(L, t, FREELIST_REF);  /* t[FREELIST_REF] = ref */
  }
}

LUALIB_API int luaL_newmetatable (lua_State *L, const char *tname)
{
    lua_getfield(L, LUA_REGISTRYINDEX, tname);  /* get registry.name */
    /* name already in use? */
    if (!lua_isnil(L, -1))
    {
        return 0;  /* leave previous value on top, but return 0 */
    }
    lua_pop(L, 1);
    lua_newtable(L);  /* create metatable */
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, tname);  /* registry.name = metatable */
    return 1;
}
