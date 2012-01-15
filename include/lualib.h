/*
** $Id: lualib.h,v 1.36.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua standard libraries
** See Copyright Notice in lua.h
*/


#ifndef lualib_h
#define lualib_h

#include "lua.h"


/* Key to file-handle type */
#define LUA_FILEHANDLE		"_File"

/**
* Callbacks for file I/O.
*/ 
typedef void*  (*luaL_FileOpen)  (lua_State* L, const char* fileName, const char* mode );
/** Returns 0 on success, EOF on failure. */
typedef int    (*luaL_FileClose) (lua_State* L, void* handle );
typedef size_t (*luaL_FileRead)  (lua_State* L, void* handle, void* dst, size_t size );
typedef size_t (*luaL_FileWrite) (lua_State* L, void* handle, const void* src, size_t size );
/** Returns the current position in the file (or -1 on error). */
typedef long   (*luaL_FileSeek)  (lua_State* L, void* handle, long offset, int origin );

typedef struct luaL_FileCallbacks luaL_FileCallbacks;

struct luaL_FileCallbacks
{
    luaL_FileOpen   open;
    luaL_FileClose  close;
    luaL_FileRead   read;
    luaL_FileWrite  write;
    luaL_FileSeek   seek;
};


#define LUA_COLIBNAME	"coroutine"
LUALIB_API int (luaopen_base) (lua_State *L);

#define LUA_TABLIBNAME	"table"
LUALIB_API int (luaopen_table) (lua_State *L);

#define LUA_IOLIBNAME	"io"
LUALIB_API int (luaopen_io) (lua_State *L );
LUALIB_API int (luaopen_iocallbacks) (lua_State *L, luaL_FileCallbacks* callbacks );

#define LUA_OSLIBNAME	"os"
LUALIB_API int (luaopen_os) (lua_State *L);

#define LUA_STRLIBNAME	"string"
LUALIB_API int (luaopen_string) (lua_State *L);

#define LUA_MATHLIBNAME	"math"
LUALIB_API int (luaopen_math) (lua_State *L);

#define LUA_DBLIBNAME	"debug"
LUALIB_API int (luaopen_debug) (lua_State *L);

#define LUA_LOADLIBNAME	"package"
LUALIB_API int (luaopen_package) (lua_State *L);

#define LUA_BITLIBNAME	"bit"
LUALIB_API int luaopen_bit(lua_State* L);

/* open all previous libraries */
LUALIB_API void (luaL_openlibs) (lua_State *L); 



#ifndef lua_assert
#define lua_assert(x)	((void)0)
#endif


#endif