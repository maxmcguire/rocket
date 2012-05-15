/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

extern "C"
{
#include "lua.h"
}

#include "State.h"
#include "Function.h"
#include "String.h"
#include "Table.h"
#include "UserData.h"
#include "Vm.h"
#include "Input.h"
#include "Code.h"

#include <string.h>

/**
 * Arguments that are passed into lua_load.
 */
struct ParseArgs
{
    lua_Reader      reader;
    void*           userdata;
    const char*     name;
};

struct Output
{
    lua_State*      L;
    lua_Writer      writer;
    void*           userData;
    int             status;
};

/** Header for a binary serialized chunk. */
struct Header
{
    char            magic[4];
    unsigned char   version;
    unsigned char   format;
    unsigned char   endianness;
    unsigned char   intSize;
    unsigned char   sizetSize;
    unsigned char   instructionSize;
    unsigned char   numberSize;
    unsigned char   integralFlag;
};

/**
 * Accepts negative and pseudo indices.
 */
static Value* GetValueForIndex(lua_State* L, int index)
{
    Value* result = NULL;
    if (index > 0)
    {
        // Absolute stack index.
        result = L->stackBase + (index - 1);
        if (result >= L->stackTop)
        {
            return &L->dummyObject;
        }
    }
    else if (index > LUA_REGISTRYINDEX)
    {
        // Stack index relative to the top of the stack.
        luai_apicheck(L, index != 0 && -index <= L->stackTop - L->stackBase);
        result = L->stackTop + index;
    }
    else if (index == LUA_GLOBALSINDEX)
    {
        // Global.
        result = &L->globals;
    }
    else if (index == LUA_REGISTRYINDEX)
    {
        // Registry.
        result = &L->registry;
    }
    else if (index == LUA_ENVIRONINDEX)
    {
        CallFrame* frame = State_GetCallFrame(L);
        if (frame->function == NULL)
        {
            Vm_Error(L, "no calling environment");
        }
        // Temporarily store the environment table in the state since we need
        // to return a pointer to a Value.
        result = &L->env;
        SetValue(result, frame->function->closure->env);
    }
    else
    {
        // C up value.
        CallFrame* frame = State_GetCallFrame(L);
        Closure* closure = frame->function->closure;
        index = LUA_GLOBALSINDEX - index;
        if (index <= closure->cclosure.numUpValues)
        {
            return &closure->cclosure.upValue[index - 1];
        }
        return &L->dummyObject;
    }
    ASSERT(result != NULL);
    return result;
}

static Table* GetCurrentEnvironment(lua_State *L)
{
    Closure* closure = Vm_GetCurrentFunction(L);
    if (closure != NULL)
    {
        return closure->env;
    }
    // Use the global table as the environment when we're not executing a
    // function.
    return L->globals.table;
}

lua_State* lua_newstate(lua_Alloc alloc, void* userdata)
{
    lua_State* L = State_Create(alloc, userdata);
    return L;
}

void lua_close(lua_State* L)
{
    State_Destroy(L);
}

static Prototype* LoadBinary(lua_State* L, Input* input, const char* name)
{

    Header header;
    size_t length = Input_ReadBlock(input, &header, sizeof(header));

    if (length < sizeof(Header))
    {
        return NULL;
    }

    // Check that the buffer is a compiled Lua chunk.
    if (header.magic[0] != '\033')
    {
        return NULL;
    }

    // Check for compatible platform
    if (header.endianness != 1 ||
        header.intSize != sizeof(int) ||
        header.sizetSize != sizeof(size_t) ||
        header.numberSize != sizeof(lua_Number))
    {
        return NULL;
    }

    char* data = Input_Read(input, &length);

    Prototype* prototype = Prototype_Create(L, data, length, name);
    Free(L, data, length);

    return prototype;

}

static void Output_WriteBlock(Output* output, const void* data, size_t length)
{
    if (output->status == 0)
    {
        output->status = output->writer( output->L, data, length, output->userData );
    }
}

template <class T>
static void Output_Write(Output* output, T value)
{
    Output_WriteBlock( output, &value, sizeof(value) );
}

static void Output_WriteString(Output* output, const String* value)
{
    // Length includes a null terminator.
    size_t length = value->length + 1;
    Output_Write( output, length );
    Output_WriteBlock( output, String_GetData(value), length );
}

static void Output_WriteByte(Output* output, unsigned char value)
{
    Output_WriteBlock( output, &value, sizeof(unsigned char) );
}

static void Output_Prototype( Output* output, Prototype* prototype )
{

    Output_WriteString( output, prototype->source );
    Output_Write( output, prototype->lineDefined );
    Output_Write( output, prototype->lastLineDefined );
    Output_WriteByte( output, prototype->numUpValues );
    Output_WriteByte( output, prototype->numParams );
    Output_WriteByte( output, prototype->varArg ? 2 : 0 );
    Output_WriteByte( output, prototype->maxStackSize );

    Output_Write( output, prototype->codeSize );
    Output_WriteBlock( output, prototype->code, prototype->codeSize * sizeof(Instruction) );

    Output_Write( output, prototype->numConstants );
    for (int i = 0; i < prototype->numConstants; ++i)
    {
        const Value* constant = &prototype->constant[i];
        if (Value_GetIsNil(constant))
        {
            Output_WriteByte( output, LUA_TNIL );
        }
        else if (Value_GetIsString(constant))
        {
            Output_WriteByte( output, LUA_TSTRING );
            Output_WriteString( output, constant->string );
        }
        else if (Value_GetIsNumber(constant))
        {
            Output_WriteByte( output, LUA_TNUMBER );
            Output_Write( output, constant->number );
        }
        else if (Value_GetIsBoolean(constant))
        {
            Output_WriteByte( output, LUA_TBOOLEAN );
            Output_WriteByte( output, constant->boolean );
        }
        else
        {
            // Unexpected constant type.
            ASSERT(0);
        }
    }

    Output_Write( output, prototype->numPrototypes );
    for (int i = 0; i < prototype->numPrototypes; ++i)
    {
        Output_Prototype( output, prototype->prototype[i] );
    }

    Output_Write( output, prototype->codeSize );
    Output_WriteBlock( output, prototype->sourceLine, prototype->codeSize * sizeof(int) );

    // List of locals (optional debug data)
    Output_Write( output, static_cast<int>(0) );

    Output_Write( output, prototype->numUpValues );
    for (int i = 0; i < prototype->numUpValues; ++i)
    {
        Output_WriteString( output, prototype->upValue[i] );
    }

}

static int DumpBinary(lua_State* L, Prototype* prototype, lua_Writer writer, void* userData)
{

    Output output;
    output.L        = L;
    output.status   = 0;
    output.userData = userData;
    output.writer   = writer;

    Header header;
    header.magic[0]         = '\033';
    header.magic[1]         = 'L';
    header.magic[2]         = 'u';
    header.magic[3]         = 'a';

    header.version          = 0x51;
    header.format           = 1;

    header.endianness       = 1;
    header.intSize          = sizeof(int);
    header.sizetSize        = sizeof(size_t);
    header.instructionSize  = sizeof(Instruction);
    header.numberSize       = sizeof(lua_Number);
    header.integralFlag     = 0;

    Output_WriteBlock( &output, &header, sizeof(Header) );
    Output_Prototype( &output, prototype );

    return output.status;

}

static void Parse(lua_State* L, void* userData)
{

    // We currently can't run the GC while parsing because some objects used by
    // the parser are not properly stored in a root location.

    ParseArgs* args = static_cast<ParseArgs*>(userData);

    Input input;
    Input_Initialize(L, &input, args->reader, args->userdata);

    Prototype* prototype = NULL;

    if (Input_PeekByte(&input) == '\033')
    {
        // The data is a pre-compiled binary.
        prototype = LoadBinary(L, &input, args->name);
    }
    else
    {
        prototype = Parse(L, &input, args->name);
    }

    ASSERT(prototype != NULL);
    PushPrototype(L, prototype);

    Table* env = L->globals.table;
    Closure* closure = Closure_Create(L, prototype, env);
    PushClosure(L, closure);

    // Initialize the up values. Typically a top level check won't have any up
    // values, but if the chunk was created using string.dump or a similar method
    // it may. These up values will cease to function once the chunk is loaded.
    for (int i = 0; i < closure->lclosure.numUpValues; ++i)
    {
        closure->lclosure.upValue[i] = NewUpValue(L);
    }

    // Remove the prototype from the stack.
    ASSERT( (L->stackTop - 2)->object == prototype );
    State_Remove(L, L->stackTop - 2);

}

LUA_API int lua_load(lua_State* L, lua_Reader reader, void* userdata, const char* name)
{

    ParseArgs args;
    args.reader     = reader;
    args.userdata   = userdata;
    args.name       = name;

    int result = Vm_RunProtected(L, Parse, L->stackTop, &args, NULL);

    if (result == LUA_ERRRUN)
    {
        result = LUA_ERRSYNTAX;
    }
    return result;

}

LUA_API int lua_dump(lua_State* L, lua_Writer writer, void* data)
{
    const Value* value = GetValueForIndex(L, -1);
    if (!Value_GetIsClosure(value) || value->closure->c)
    {
        return 1;
    }
    return DumpBinary( L, value->closure->lclosure.prototype, writer, data );
}

LUA_API int lua_error(lua_State* L)
{
    State_Error(L);
    return 0;
}

LUA_API void lua_call(lua_State* L, int nargs, int numResults)
{
    Value* value = L->stackTop - (nargs + 1);
    Vm_Call(L, value, nargs, numResults);
}

LUA_API int lua_pcall(lua_State* L, int numArgs, int numResults, int errFunc)
{
    Value* value = L->stackTop - (numArgs + 1);
    Value* errHandler = NULL;
    if (errFunc != 0)
    {        
        errHandler = GetValueForIndex(L, errFunc);
    }
    return Vm_ProtectedCall(L, value, numArgs, numResults, errHandler);
}

LUA_API void lua_pushnil(lua_State* L)
{
    PushNil(L);
}

LUA_API void lua_pushnumber(lua_State *L, lua_Number n)
{
    PushNumber(L, n);
}

LUA_API void lua_pushinteger (lua_State *L, lua_Integer n)
{
    PushNumber( L, static_cast<lua_Number>(n) );
}

LUA_API void lua_pushlstring(lua_State *L, const char* data, size_t length)
{
    String* string = String_Create(L, data, length);
    PushString(L, string);
}

LUA_API void lua_pushstring(lua_State* L, const char* data)
{
    if (data == NULL)
    {
        lua_pushnil(L);
        return;
    }
    lua_pushlstring(L, data, strlen(data)); 
}

LUA_API const char* lua_pushfstring(lua_State* L, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    const char* result = lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    return result;
}

LUA_API const char* lua_pushvfstring(lua_State *L, const char *fmt, va_list argp)
{
    PushVFString(L, fmt, argp);
    return GetString(L->stackTop - 1);
}

LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction f, int n)
{
    Table* env = GetCurrentEnvironment(L);
    Closure* closure = Closure_Create(L, f, L->stackTop - n, n, env);
    Pop(L, n);
    PushClosure(L, closure);
}

LUA_API void lua_pushboolean(lua_State* L, int b)
{
    PushBoolean(L, b != 0);
}

LUA_API void lua_pushlightuserdata (lua_State *L, void *p)
{
    PushLightUserdata(L, p);
}

LUA_API void lua_pushtypename(lua_State* L, int type)
{
    String* typeName = State_TypeName(L, type);
    PushString(L, typeName);
}

LUA_API void lua_pushvalue(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    PushValue(L, value);
}

LUA_API void lua_remove(lua_State *L, int index)
{
    Value* p = GetValueForIndex(L, index);
    State_Remove(L, p);
}

void lua_setfield(lua_State* L, int index, const char* name)
{
    Value key;
    SetValue( &key, String_Create(L, name, strlen(name)) );

    Value* table = GetValueForIndex(L, index);
    Vm_SetTable( L, table, &key, L->stackTop - 1 );
    Pop(L, 1);
}

void lua_gettable(lua_State *L, int index)
{
    Value* key   = GetValueForIndex( L, -1 );
    Value* table = GetValueForIndex( L, index );
    Vm_GetTable(L, table, key, L->stackTop - 1, false);
}

void lua_getfield(lua_State *L, int index, const char* name)
{
    Value key;
    SetValue( &key, String_Create(L, name, strlen(name)) );
    Value* table = GetValueForIndex( L, index );
    Vm_GetTable(L, table, &key, L->stackTop, false);
    ++L->stackTop;
}

int lua_isnumber(lua_State* L, int index)
{
    lua_Number result;
    const Value* value = GetValueForIndex(L, index);
    return Vm_GetNumber(value, &result);
}

int lua_isstring(lua_State* L, int index)
{
    int type = lua_type(L, index);
    return type == LUA_TSTRING || type == LUA_TNUMBER;
}

int lua_iscfunction(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    return Value_GetIsClosure(value) && value->closure->c;
}

int lua_isuserdata(lua_State* L, int index)
{
    return lua_type(L, index) == LUA_TUSERDATA;
}

LUA_API lua_Number lua_tonumber(lua_State *L, int index)
{
    lua_Number result;
    const Value* value = GetValueForIndex(L, index);
    if (Vm_GetNumber(value, &result))
    {
        return result;
    }
    return 0.0;
}

LUA_API lua_Integer lua_tointeger(lua_State *L, int index)
{
    lua_Number  d = lua_tonumber(L, index);
    lua_Integer i;
    lua_number2integer(i, d);
    return i;
}

LUA_API int lua_toboolean(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    return Vm_GetBoolean(value);
}

LUA_API const char* lua_tolstring(lua_State *L, int index, size_t* length )
{
    Value* value = GetValueForIndex(L, index);
    if (ToString(L, value))
    {
        const String* string = value->string;
        if (length != NULL)
        {
            *length = string->length;
        }
        return String_GetData(string);
    }
    return NULL;
}

LUA_API lua_CFunction lua_tocfunction(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    if (Value_GetIsClosure(value))
    {
        Closure* closure = value->closure;
        if (closure->c)
        {
            return closure->cclosure.function;
        }
    }
    return NULL;
}

LUA_API const void* lua_topointer(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    switch (value->tag)
    {
    case Tag_Table:
    case Tag_Closure:
    case Tag_Thread:
        return value->object;
    case Tag_LightUserdata:
        return value->lightUserdata;
    case Tag_Userdata:
        return UserData_GetData(value->userData);
    }
    return NULL;
}

LUA_API void* lua_touserdata(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    if (value->tag == Tag_LightUserdata)
    {
        return value->lightUserdata;
    }
    else if (value->tag == Tag_Userdata)
    {
        return UserData_GetData(value->userData);
    }
    return NULL;
}

LUA_API size_t lua_objlen(lua_State* L, int index)
{
    Value* value = GetValueForIndex(L, index);
    if (Value_GetIsTable(value))
    {
        return Table_GetSize(L, value->table);
    }
    else if (Value_GetIsUserData(value))
    {
        return value->userData->size;
    }
    else if (Value_GetIsString(value) || (Value_GetIsNumber(value) && ToString(L, value)))
    {
        return value->string->length;
    }
    return 0;
}

LUA_API void lua_rawget(lua_State* L, int index)
{

    Value* table = GetValueForIndex( L, index );
    luai_apicheck(L, Value_GetIsTable(table) );

    const Value* key = GetValueForIndex(L, -1);
    const Value* value = Table_GetTable(L, table->table, key);

    if (value != NULL)
    {
        *(L->stackTop - 1) = *value;
    }
    else
    {
        SetNil(L->stackTop - 1);
    }

}

LUA_API void lua_rawgeti(lua_State *L, int index, int n)
{

    Value* table = GetValueForIndex(L, index);
    luai_apicheck(L, Value_GetIsTable(table) );    

    const Value* value = Table_GetTable(L, table->table, n);
    if (value == NULL)
    {
        PushNil(L);
    }
    else
    {
        PushValue(L, value);
    }

}

LUA_API void lua_rawset(lua_State *L, int index)
{

    Value* key   = GetValueForIndex(L, -2);
    Value* value = GetValueForIndex(L, -1);
    Value* table = GetValueForIndex(L, index);

    luai_apicheck(L, Value_GetIsTable(table) );
    Table_SetTable( L, table->table, key, value );
    Pop(L, 2);

}

LUA_API void lua_rawseti(lua_State* L, int index, int n)
{

    Value* table = GetValueForIndex(L, index);
    luai_apicheck(L, Value_GetIsTable(table) );    

    Value* value = GetValueForIndex(L, -1);
    Table_SetTable(L, table->table, n, value);
    Pop(L, 1);

}

LUA_API void lua_settable(lua_State* L, int index)
{
    Value* key   = GetValueForIndex(L, -2);
    Value* value = GetValueForIndex(L, -1);
    Value* table = GetValueForIndex(L, index);
    Vm_SetTable( L, table, key, value );
    Pop(L, 2);
}

LUA_API int lua_type(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    if (value == &L->dummyObject)
    {
        return LUA_TNONE;
    }
    return Value_GetType(value);
}

LUA_API const char* lua_typename(lua_State* L, int type)
{
    return String_GetData( State_TypeName(L, type) );
}

int lua_rawequal(lua_State* L, int index1, int index2)
{
    const Value* value1 = GetValueForIndex(L, index1);
    const Value* value2 = GetValueForIndex(L, index2);
    return Value_Equal(value1, value2);
}

int lua_lessthan(lua_State *L, int index1, int index2)
{
    const Value* value1 = GetValueForIndex(L, index1);
    const Value* value2 = GetValueForIndex(L, index2);
    return Vm_Less(L, value1, value2);
}

LUA_API int lua_gettop(lua_State* L)
{
    return static_cast<int>(L->stackTop - L->stackBase);
}

LUA_API void lua_settop(lua_State *L, int index)
{
    if (index < 0)
    {
        L->stackTop += index + 1;
        luai_apicheck(L, L->stackTop >= L->stackBase );
    }
    else
    {
        Value* value = L->stackTop;
        Value* top = L->stackBase + index;
        while (value < top)
        {
            SetNil(value);
            ++value;
        }
        L->stackTop = top;
    }
}

LUA_API void lua_insert(lua_State *L, int index)
{
    Value* p = GetValueForIndex(L, index);
    for (Value* q = L->stackTop; q > p; q--)
    {
        *q = *(q - 1);
    }
    *p = *L->stackTop;
}

void lua_replace(lua_State *L, int index)
{
    if (index == LUA_ENVIRONINDEX)
    {
        // Special case for the environment index since we can only assign table
        // values, and the value returned by GetValueForIndex is a copy.
        CallFrame* frame = State_GetCallFrame(L);
        if (frame->function == NULL)
        {
            Vm_Error(L, "no calling environment");
        }
        const Value* src = L->stackTop - 1;
        luai_apicheck(L, Value_GetIsTable(src));
        frame->function->closure->env = src->table;
        --L->stackTop;
    }
    else
    {
        Value* dst = GetValueForIndex(L, index);
        --L->stackTop;
        *dst = *L->stackTop;
    }
}

int lua_checkstack(lua_State *L, int size)
{
    // lua_checkstack just reserves space for us on the stack, and since we're
    // not checking for stack overflow, we don't need to do anything.
    return 1;
}

void lua_createtable(lua_State *L, int narr, int nrec)
{
    Value value;
    SetValue( &value, Table_Create(L) );
    PushValue( L, &value );
}

void lua_concat(lua_State *L, int n)
{
    Concat(L, n);
}

int lua_getstack(lua_State* L, int level, lua_Debug* ar)
{
    int callStackSize = Vm_GetCallStackSize(L);
    // Get the absolute index in the call stack.
    int activeFunction = callStackSize - level - 1;
    if (activeFunction < 0)
    {
        return 0;
    }
    ar->activeFunction = activeFunction;
    return 1;
}

int lua_getinfo(lua_State* L, const char* what, lua_Debug* ar)
{

    const Closure* function = NULL;
    const CallFrame* frame  = NULL;
    
    if (what[0] == '>')
    {
        const Value* value = L->stackTop - 1;
        luai_apicheck(L, Value_GetIsClosure(value) );
        function = value->closure;
        ++what;
    }
    else
    {
        luai_apicheck(L, ar->activeFunction < Vm_GetCallStackSize(L) );
        frame = L->callStackBase + ar->activeFunction;
        if (frame->function != NULL)
        {
            ASSERT( Value_GetIsClosure(frame->function) );
            function = frame->function->closure;
        }
    }

    int result = 1;

    while (what[0] != 0)
    {

        switch (what[0])
        {
        case 'n':
            ar->name            = NULL;
            ar->namewhat        = "";
            break;
        case 'S':
            ar->source          = NULL;
            ar->short_src[0]    = 0;
            ar->linedefined     = -1;
            ar->lastlinedefined = -1;
            if (function == NULL)
            {
                ar->what   = "main";
                ar->source = NULL;
            }
            else if (function->c)
            {
                ar->what    = "C";
                ar->source = "=[C]";;
            }
            else
            {
                ar->what  = "Lua";
                ar->source = String_GetData(function->lclosure.prototype->source);
                Prototype_GetName(function->lclosure.prototype, ar->short_src, LUA_IDSIZE);
            }
            break;
        case 'l':
            if (function == NULL || function->c)
            {
                ar->currentline = -1;
            }
            else
            {
                size_t ip = frame->ip - function->lclosure.prototype->code;
                ar->currentline = function->lclosure.prototype->sourceLine[ip];
            }
            break;
        case 'u':
            if (function != NULL)
            {
                ar->nups = 0;
            }
            else if (function->c)
            {
                ar->nups = function->cclosure.numUpValues;
            }
            else
            {
                ar->nups = function->lclosure.numUpValues;
            }
            break;
        case 'f':
            // Pushes onto the stack the function that is running at the given level
            PushValue(L, frame->function);
            break;
        case 'L':
            //  pushes onto the stack a table whose indices are the numbers of the lines that
            // are valid on the function. (A valid line is a line with some associated code,
            // that is, a line where you can put a break point. Non-valid lines include empty
            // lines and comments
            ASSERT(0);
            break;
        default:
            result = 0;
        }
        ++what;
    }

    return result;

}

const char* lua_getupvalue(lua_State *L, int funcIndex, int n)
{

    const Value* func = GetValueForIndex(L, funcIndex);
    luai_apicheck(L, Value_GetIsClosure(func) );

    Closure* closure = func->closure;

    if (closure->c)
    {
        if (n >= 1 && n <= closure->cclosure.numUpValues)
        {
            PushValue(L, &closure->cclosure.upValue[n - 1]);
            // Up values to a C function are unnamed.
            return "";
        }
    }
    else
    {
        if (n >= 1 && n <= closure->lclosure.numUpValues)
        {
            PushValue(L, closure->lclosure.upValue[n - 1]->value);
            // Get the name of the up value from the prototype.
            String* name = closure->lclosure.prototype->upValue[n - 1];
            return String_GetData(name);
        }
    }
    return NULL;

}

int lua_next(lua_State* L, int index)
{

    Value* table = GetValueForIndex(L, index);
    luai_apicheck(L, Value_GetIsTable(table) );
    
    Value* key = GetValueForIndex(L, -1);

    const Value* value = Table_Next(table->table, key);
    if (value == NULL)
    {
        Pop(L, 1);
        return 0;
    }
    PushValue(L, value);
    return 1;

}

void* lua_newuserdata(lua_State* L, size_t size)
{
    Table* env = GetCurrentEnvironment(L);
    UserData* userData = UserData_Create(L, size, env);
    PushUserData(L, userData);
    return UserData_GetData(userData);
}

int lua_setmetatable(lua_State* L, int index)
{
    Value* object    = GetValueForIndex(L, index);
    Value* metatable = GetValueForIndex(L, -1);

    Table* table = NULL;
    if (!Value_GetIsNil(metatable))
    {
        luai_apicheck(L, Value_GetIsTable(metatable) );
        table = metatable->table;
    }

    Value_SetMetatable( L, object, table );

    Pop(L, 1);
    return 1;
}

int lua_getmetatable(lua_State* L, int index)
{

    const Value* object = GetValueForIndex(L, index);
    Table* metatable = Value_GetMetatable(L, object);

    if (metatable == NULL)
    {
        return 0;
    }

    PushTable(L, metatable);
    return 1;

}

int lua_setfenv(lua_State *L, int index)
{
    Value* object = GetValueForIndex(L, index);
    Value* env = GetValueForIndex(L, -1);
    
    luai_apicheck(L, Value_GetIsTable(env) );

    int result = Value_SetEnv(L, object, env->table);

    Pop(L, 1);
    return result;
}

void lua_getfenv(lua_State *L, int index)
{
    const Value* object = GetValueForIndex(L, index);
    Table* table = Value_GetEnv(object);
    if (table == NULL)
    {
        PushNil(L);
    }
    else
    {
        SetValue(L->stackTop, table);
        ++L->stackTop;
    }
}

int lua_gc(lua_State* L, int what, int data)
{
    if (what == LUA_GCCOLLECT)
    {
        Gc_Collect(L, &L->gc);
        return 1;
    }
    else if (what == LUA_GCSTEP)
    {
        if (Gc_Step(L, &L->gc))
        {
            return 1;
        }
    }
    /*
    else if (what == LUA_GCSETPAUSE)
    {
        // TODO: Implement this!
        assert(0);
        return 0;
    }
    */
    else if (what == LUA_GCCOUNT)
    {
        return static_cast<int>(L->totalBytes / 1024);
    }
    else if (what == LUA_GCCOUNTB)
    {
        return static_cast<int>(L->totalBytes % 1024);
    }
    return 0;
}

void lua_setgchook(lua_State *L, lua_GCHook func)
{
    L->gchook = func;
}

int lua_sethook(lua_State *L, lua_Hook hook, int mask, int count)
{
    if (hook == NULL || mask == 0)
    {
        L->hookMask = 0;
        L->hook     = NULL;
    }
    else
    {
        L->hook     = hook;
        L->hookMask = mask;
    }
    L->hookCount = count;
    return 1;
}

lua_Hook lua_gethook(lua_State* L)
{
    return L->hook;
}

int lua_gethookmask(lua_State* L)
{
  return L->hookMask;
}

int lua_gethookcount(lua_State* L)
{
    return L->hookCount;
}

LUA_API lua_CFunction lua_atpanic(lua_State* L, lua_CFunction panic)
{
    lua_CFunction old;
    old = L->panic;
    L->panic = panic;
    return old;
}

LUA_API int lua_pushthread(lua_State* L)
{
    // Not yet implemented.
    ASSERT(0);
    return 0;
}

LUA_API lua_State* lua_tothread(lua_State* L, int index)
{
    // Not yet implemented.
    ASSERT(0);
    return 0;
}

LUA_API lua_State* lua_newthread(lua_State* L)
{
    // Not yet implemented.
    ASSERT(0);
    return 0;
}

LUA_API int lua_yield(lua_State* L, int nresults)
{
    // Not yet implemented.
    ASSERT(0);
    return 0;
}

LUA_API int lua_resume(lua_State *L, int narg)
{
    // Not yet implemented.
    ASSERT(0);
    return 0;
}

LUA_API int lua_status(lua_State *L)
{
    // Not yet implemented.
    ASSERT(0);
    return 0;
}

LUA_API void lua_setlevel(lua_State* from, lua_State* to)
{
    // Not yet implemented.
    ASSERT(0);
}

LUA_API void lua_xmove(lua_State* from, lua_State* to, int n)
{
    // Not yet implemented.
    ASSERT(0);
}

LUA_API const char* lua_getlocal(lua_State* L, const lua_Debug* ar, int n)
{
    // Not yet implemented.
    return 0;
}

LUA_API const char* lua_setlocal (lua_State *L, const lua_Debug* ar, int n)
{
    // Not yet implemented.
    ASSERT(0);(0);
    return 0;
}

/**
 * Returns the name of the up value (for a Lua function) and upValue is set to
 * point to the address of where the up values value is stored.
 */
static const char* GetUpValue(Value* value, int n, Value** upValue)
{

    if (!Value_GetIsClosure(value))
    {
        return NULL;
    }

    Closure* closure = value->closure;
    
    if (closure->c)
    {
        if (n >= 1 && n <= closure->cclosure.numUpValues)
        {
            *upValue = &closure->cclosure.upValue[n - 1];
            // Up values to a C function are unnamed.
            return "";
        }
    }
    else
    {
        if (n >= 1 && n <= closure->lclosure.numUpValues)
        {
            *upValue = closure->lclosure.upValue[n - 1]->value;
            return String_GetData( closure->lclosure.prototype->upValue[n - 1] );   
        }
    }

    return NULL;
  
}

LUA_API const char* lua_setupvalue(lua_State* L, int funcindex, int n)
{

    Value* closure = GetValueForIndex(L, funcindex);

    Value* upValue;
    const char* name = GetUpValue(closure, n, &upValue);
    
    if (name != NULL)
    {
        ASSERT(upValue != NULL);
        Value_Copy( upValue, L->stackTop - 1 );
        Pop(L, 1);
    }

    return name;
}
