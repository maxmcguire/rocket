#include "Test.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

extern "C" void clear();

int main(int argc, char* argv[])
{
    Test_RunTests("TableConstructor");
    return 0;
}