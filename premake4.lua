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

-- Main library
project "Rocket"
    kind "SharedLib"
    location "build"
    language "C++"
    files { "src/*.h", "src/*.c", "src/*.cpp", "src/*.asm", "src/*.def", "include/*.h" }
    includedirs { "include" }
    links { "AuxLib" }
    defines { "ROCKET_EXPORTS", "LUA_CORE" }

    configuration "Debug"
        defines { "DEBUG" }
        flags { "Symbols" }
        targetdir "bin/debug"

    configuration "Release"
        defines { "NDEBUG" }
        flags { "Optimize" }
        targetdir "bin/release"   
     
-- Auxiliary library     
project "AuxLib"
    kind "StaticLib"
    location "build"
    language "C++"
    files { "src/AuxLib/*.h", "src/AuxLib/*.c", "src/AuxLib/*.cpp" }
    includedirs { "include" }

    configuration "Debug"
        defines { "DEBUG" }
        flags { "Symbols" }
        targetdir "bin/debug"

    configuration "Release"
        defines { "NDEBUG" }
        flags { "Optimize" }
        targetdir "bin/release"         

-- Unit test     
project "Test"
    kind "ConsoleApp"
    location "build"
    language "C++"
    files { "src/Test/*.h", "src/Test/*.c", "src/Test/*.cpp" }
    includedirs { "include" }
    links { "Rocket" }

    configuration "Debug"
        defines { "DEBUG" }
        flags { "Symbols" }
        targetdir "bin/debug"

    configuration "Release"
        defines { "NDEBUG" }
        flags { "Optimize" }     
        targetdir "bin/release"