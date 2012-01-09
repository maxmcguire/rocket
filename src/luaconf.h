#ifndef LUACONF_H
#define LUACONF_H

#if defined(ROCKET_EXPORTS)
#define LUA_API __declspec(dllexport)
#else
#define LUA_API __declspec(dllimport)
#endif

/* more often than not the libs go together with the core */
#define LUALIB_API	LUA_API

#define LUA_NUMBER_SCAN		"%lf"
#define LUA_NUMBER_FMT		"%.14g"
#define lua_number2str(s,n)	sprintf((s), LUA_NUMBER_FMT, (n))
#define LUAI_MAXNUMBER2STR	32 /* 16 digits, sign, point, and \0 */
#define lua_str2number(s,p)	strtod((s), (p))

/*
@@ The luai_num* macros define the primitive operations over numbers.
*/
#include <math.h>
#define luai_numadd(a,b)	((a)+(b))
#define luai_numsub(a,b)	((a)-(b))
#define luai_nummul(a,b)	((a)*(b))
#define luai_numdiv(a,b)	((a)/(b))
#define luai_nummod(a,b)	((a) - floor((a)/(b))*(b))
#define luai_numpow(a,b)	(pow(a,b))
#define luai_numunm(a)		(-(a))
#define luai_numeq(a,b)		((a)==(b))
#define luai_numlt(a,b)		((a)<(b))
#define luai_numle(a,b)		((a)<=(b))
#define luai_numisnan(a)	(!luai_numeq((a), (a)))

/*
@@ LUA_INTEGER is the integral type used by lua_pushinteger/lua_tointeger.
** CHANGE that if ptrdiff_t is not adequate on your machine. (On most
** machines, ptrdiff_t gives a good choice between int or long.)
*/
#define LUA_INTEGER	int

/*
@@ LUA_IDSIZE gives the maximum size for the description of the source
@* of a function in debug information.
** CHANGE it if you want a different size.
*/
#define LUA_IDSIZE	60

// Maximum length of an identifier.
#define LUA_MAXNAME 512

// Maximum number of variables that can assigned in a single statement.
#define LUA_MAXASSIGNS 100

/*
@@ LUAL_BUFFERSIZE is the buffer size used by the lauxlib buffer system.
*/
#define LUAL_BUFFERSIZE		BUFSIZ

/*
@@ LUAI_MAXVARS is the maximum number of local variables per function
@* (must be smaller than 250).
*/
#define LUAI_MAXVARS		200

/*
@@ LUAI_MAXUPVALUES is the maximum number of upvalues per function
@* (must be smaller than 250).
*/
#define LUAI_MAXUPVALUES	60
/*
@@ LUAI_MAXCCALLS is the maximum depth for nested C calls (short) and
@* syntactical nested non-terminals in a program.
*/
#define LUAI_MAXCCALLS		200

#endif