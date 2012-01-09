/*
 * RocketVM
 * Copyright (c) 2011-2012 Max McGuire
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef LUA_H
#define LUA_H

#include "luaconf.h"

#include <stdarg.h>

#define LUA_VERSION	        "RocketVM 5.1"
#define LUA_RELEASE	        "RocketVM 5.1.4"
#define LUA_VERSION_NUM	    501

// Types.
#define LUA_TNONE		    (-1)
#define LUA_TNIL		    0
#define LUA_TBOOLEAN		1
#define LUA_TLIGHTUSERDATA	2
#define LUA_TNUMBER		    3
#define LUA_TSTRING		    4
#define LUA_TTABLE		    5
#define LUA_TFUNCTION		6
#define LUA_TUSERDATA		7
#define LUA_TTHREAD		    8

// Internally used types. These are never placed on the stack and therefore
// not visible outside the VM.
#define LUA_TPROTOTYPE      9

// Return values.
#define LUA_YIELD	        1
#define LUA_ERRRUN	        2
#define LUA_ERRSYNTAX	    3
#define LUA_ERRMEM	        4
#define LUA_ERRERR	        5

// Pseudo-indices
#define LUA_REGISTRYINDEX	(-10000)
#define LUA_ENVIRONINDEX	(-10001)
#define LUA_GLOBALSINDEX	(-10002)
#define lua_upvalueindex(i)	(LUA_GLOBALSINDEX-(i))

// Option for multiple returns in 'lua_pcall' and 'lua_call'
#define LUA_MULTRET	(-1)

// Minimum Lua stack available to a C function.
#define LUA_MINSTACK	20

// Type for integer functions.
typedef LUA_INTEGER lua_Integer;

// Opaque type storing the VM state.
struct lua_State;

typedef double lua_Number;
typedef int (*lua_CFunction)(lua_State* L);

typedef void* (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
typedef const char* (*lua_Reader)(lua_State* L, void* ud, size_t* sz);

lua_State* lua_newstate(lua_Alloc f, void* ud);

LUA_API void lua_close(lua_State* L);

// Miscellaneous functions
LUA_API int   (lua_error) (lua_State *L);
LUA_API int   (lua_next) (lua_State *L, int idx);
LUA_API void  (lua_concat) (lua_State *L, int n);
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud);

// Load and run Lua code
LUA_API void  (lua_call) (lua_State *L, int nargs, int nresults);
LUA_API int   (lua_pcall) (lua_State *L, int nargs, int nresults, int errfunc);
LUA_API int   (lua_cpcall) (lua_State *L, lua_CFunction func, void *ud);
LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt, const char *chunkname);

// Basic stack manipulation
LUA_API int   (lua_gettop) (lua_State *L);
LUA_API void  (lua_settop) (lua_State *L, int idx);
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);
LUA_API void  (lua_pop) (lua_State* L, int num);
LUA_API void  (lua_remove) (lua_State *L, int idx);
LUA_API void  (lua_insert) (lua_State *L, int idx);
LUA_API void  (lua_replace) (lua_State *L, int idx);
LUA_API int   (lua_checkstack) (lua_State *L, int sz);
LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);

// Push functions (C -> stack)
LUA_API void  lua_pushnil (lua_State *L);
LUA_API void  lua_pushnumber (lua_State *L, lua_Number n);
LUA_API void  lua_pushinteger (lua_State *L, lua_Integer n);
LUA_API void  lua_pushlstring (lua_State *L, const char *s, size_t l);
LUA_API void  lua_pushstring (lua_State *L, const char *s);
LUA_API const char* lua_pushvfstring (lua_State *L, const char *fmt, va_list argp);
LUA_API const char* lua_pushfstring (lua_State *L, const char *fmt, ...);
LUA_API void  lua_pushcclosure (lua_State *L, lua_CFunction fn, int n);
LUA_API void  lua_pushcfunction (lua_State* L, lua_CFunction f);
LUA_API void  lua_pushboolean (lua_State *L, int b);
LUA_API void  lua_pushlightuserdata (lua_State *L, void *p);
LUA_API int   lua_pushthread (lua_State *L);

// Access functions (stack -> C)

LUA_API int             (lua_isnumber) (lua_State *L, int idx);
LUA_API int             (lua_isstring) (lua_State *L, int idx);
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);
LUA_API int             (lua_type) (lua_State *L, int idx);
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

LUA_API int            (lua_equal) (lua_State *L, int idx1, int idx2);
LUA_API int            (lua_rawequal) (lua_State *L, int idx1, int idx2);
LUA_API int            (lua_lessthan) (lua_State *L, int idx1, int idx2);

LUA_API lua_Number      (lua_tonumber) (lua_State *L, int idx);
LUA_API lua_Integer     (lua_tointeger) (lua_State *L, int idx);
LUA_API int             (lua_toboolean) (lua_State *L, int idx);
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);
LUA_API const char*     lua_tostring (lua_State* L, int index);
LUA_API size_t          (lua_objlen) (lua_State *L, int idx);
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);
LUA_API void	       *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);

// Get functions (Lua -> stack)
LUA_API void  (lua_gettable) (lua_State *L, int idx);
LUA_API void  (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_rawget) (lua_State *L, int idx);
LUA_API void  (lua_rawgeti) (lua_State *L, int idx, int n);
LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);
LUA_API void  (lua_getfenv) (lua_State *L, int idx);
 
// Set functions (stack -> Lua)
LUA_API void  (lua_settable) (lua_State *L, int idx);
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_rawset) (lua_State *L, int idx);
LUA_API void  (lua_rawseti) (lua_State *L, int idx, int n);
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);
LUA_API int   (lua_setfenv) (lua_State *L, int idx);

LUA_API void lua_register(lua_State *L, const char *name, lua_CFunction f);

LUA_API void lua_setfield(lua_State *L, int index, const char* name);
LUA_API void lua_getfield(lua_State *L, int index, const char* name);

LUA_API void lua_setglobal(lua_State *L, const char* name);
LUA_API void lua_getglobal(lua_State *L, const char *name);

LUA_API int lua_type(lua_State* L, int index);
LUA_API const char* lua_typename(lua_State *L, int tp);

#define lua_pushliteral(L, s) \
	lua_pushlstring(L, "" s, (sizeof(s)/sizeof(char))-1)

LUA_API void lua_newtable(lua_State* L);

LUA_API int lua_isfunction(lua_State* L, int n);
LUA_API int lua_istable(lua_State* L, int n);
LUA_API int lua_islightuserdata(lua_State* L, int n);
LUA_API int lua_isnil(lua_State* L, int n);
LUA_API int lua_isboolean(lua_State* L, int n);
LUA_API int lua_isthread(lua_State* L, int n);
LUA_API int lua_isnone(lua_State* L, int n);
LUA_API int lua_isnoneornil(lua_State* L, int n);

LUA_API void  (lua_concat) (lua_State *L, int n);

/*
** {======================================================================
** Debug API
** =======================================================================
*/

// Event codes
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILRET 4

// Event masks
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

typedef struct lua_Debug lua_Debug;  /* activation record */


// Functions to be called by the debuger in specific events
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


LUA_API int lua_getstack (lua_State *L, int level, lua_Debug *ar);
LUA_API int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar);
LUA_API const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n);
LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n);

LUA_API int lua_sethook (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook lua_gethook (lua_State *L);
LUA_API int lua_gethookmask (lua_State *L);
LUA_API int lua_gethookcount (lua_State *L);


struct lua_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) `global', `local', `field', `method' */
  const char *what;	/* (S) `Lua', `C', `main', `tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int nups;		/* (u) number of upvalues */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  int i_ci;  /* active function */
};

/*
** garbage-collection function and options
*/

#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7

LUA_API int (lua_gc) (lua_State *L, int what, int data);

#endif