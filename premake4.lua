buildmasm = true

solution "Rocket"
    configurations { "Debug", "Release" }
    location "build"
    defines { "_CRT_SECURE_NO_WARNINGS", "_CRT_SECURE_NO_DEPRECATE" }
    vpaths { 
        ["Header Files"] = "**.h",
        ["Source Files"] = { "**.cpp", "**.c", "**.asm" },
        ["Linker Files"] = { "**.def" }
    }
	
    configuration "Debug"
        defines { "DEBUG" }
        flags { "Symbols", "NoExceptions", "NoRTTI" }
        targetdir "bin/debug"

    configuration "Release"
        defines { "NDEBUG" }
        flags { "Symbols", "Optimize", "EnableSSE2", "NoExceptions", "NoRTTI" }
        targetdir "bin/release"      	

-- Main library
project "Rocket"
    kind "SharedLib"
    location "build"
    language "C++"
    files { "src/*.h", "src/*.c", "src/*.cpp", "src/*.asm", "src/*.def", "include/*.h" }
    includedirs { "include" }
    links { "AuxLib" }
    defines { "ROCKET_EXPORTS", "LUA_CORE" }
     
-- Auxiliary library     
project "AuxLib"
    kind "StaticLib"
    location "build"
    language "C++"
    files { "src/AuxLib/*.h", "src/AuxLib/*.c", "src/AuxLib/*.cpp" }
    includedirs { "include" }

-- Unit test     
project "Test"
    kind "ConsoleApp"
    location "build"
    language "C++"
    files { "src/Test/*.h", "src/Test/*.c", "src/Test/*.cpp" }
    includedirs { "include" }
    links { "Rocket" }