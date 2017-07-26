
local THISDIR = os.getcwd()
local CLANG_PATH
if os.is("linux") then
    CLANG_PATH = THISDIR .. "/clang/bin:/usr/local/bin:/usr/bin"
elseif os.is("windows") then
    CLANG_PATH = THISDIR .. "/clang/bin:/msys64/mingw64/bin"
else
    error("unsupported os")
end

local function flatten(t)
    local result = {}
    local function iterate(t)
        for _,k in pairs(t) do
            if type(k) == "table" then 
                iterate(k)
            elseif k ~= nil and k ~= "" then
                table.insert(result, k)
            end
        end
    end
    iterate(t)
    return result
end

local function pkg_config(cmd)
    local args = os.outputof(cmd)
    return flatten(string.explode(args, "[ \n]+"))
end

local function toolpath(name, searchpath)
    local path = os.pathsearch(name, searchpath)
    assert(path, name .. " missing")
    return path .. "/" .. name
end

local function print_list(l)
    for k,v in pairs(l) do
        print(k,v)
    end
end

local CLANG_CXX = toolpath("clang++", CLANG_PATH)
local LLVM_CONFIG = toolpath("llvm-config", CLANG_PATH)

LLVM_LDFLAGS = pkg_config(LLVM_CONFIG .. " --ldflags")
LLVM_CXXFLAGS = pkg_config(LLVM_CONFIG .. " --cxxflags")
LLVM_LIBS = pkg_config(LLVM_CONFIG .. " --libs engine passes option objcarcopts coverage support lto coroutines")

premake.gcc.cxx = CLANG_CXX
premake.gcc.llvm = true

solution "scopes"
    location "build"
    configurations { "debug", "release" }
    platforms { "native", "x64" }

--[[    
configuration { "vs*" }
    buildoptions {
        "/D_CRT_SECURE_NO_WARNINGS",
        "/wd4100",  -- unreferenced formal parameter
        "/wd4201",  -- nonstandard extension used : nameless struct/union
        "/wd4244",  --conversion from 'double' to 'float'
        "/wd4305",  -- 'initializing' : truncation from 'double' to 'float'
    }

configuration { "gmake" }
    linkoptions {
        "-Wl,-rpath,'$$ORIGIN'",
    -- "-Wl,-rpath-link,'$$ORIGIN'",
    
    -- the ones below will fail when linking
    -- to GLEW, which then delay-loads GL
    -- "-Wl,--no-undefined",
    -- "-Wl,--allow-shlib-undefined"
    }
    buildoptions {
        -- "-Wextra",
        "-Wno-unused-parameter",
        "-fno-strict-aliasing"
    }
--]]

project "scopes"
    kind "ConsoleApp"
    language "C++"
    files {
        "scopes.cpp", 
        "external/linenoise-ng/src/linenoise.cpp",
        "external/linenoise-ng/src/ConvertUTF.cpp",
        "external/linenoise-ng/src/wcwidth.cpp"        
    }
    includedirs {
        "external/linenoise-ng/include",
        "libffi/include",
    }
    libdirs {
        --"bin",
        --"build/src/nanovg/build",
        --"build/src/tess2/Build",
        --"build/src/stk/src",
        --"build/src/nativefiledialog/src",
    }
    targetdir "bin"
    defines {
        "SCOPES_CPP_IMPL",
        "SCOPES_MAIN_CPP_IMPL",
    }

    buildoptions {
        "-std=c++11",
        "-fno-rtti",
        "-fno-exceptions",
        "-ferror-limit=1",
        "-pedantic",
        "-Wall",
        "-Wno-keyword-macro",
    }
    buildoptions(LLVM_CXXFLAGS)

    configuration { "linux" }
        defines {
            "_GLIBCXX_USE_CXX11_ABI=0",
        }

        links { 
            "pthread", "m", "tinfo", "dl", "z",
            "clangFrontend",
            "clangDriver",
            "clangSerialization",
            "clangCodeGen",
            "clangParse",
            "clangSema",
            "clangAnalysis",
            "clangEdit",
            "clangAST",
            "clangLex",
            "clangBasic"
        }
        
        linkoptions {
            --"-Wl,--whole-archive",
            --"-l...",
            --"-Wl,--no-whole-archive",
            
            --"-Wl,--export-dynamic",            
            --"-rdynamic",
            THISDIR .. "/libffi/.libs/libffi.a",
        }
        linkoptions(LLVM_LDFLAGS)
        linkoptions { "-Wl,--whole-archive" }
        linkoptions(LLVM_LIBS)
        linkoptions { "-Wl,--no-whole-archive" }

        postbuildcommands {
            "cp -v " .. THISDIR .. "/bin/scopes " .. THISDIR
        }
    
    --[[
    configuration { "windows" }
        links { "nanovg", "stk", "tess2", "glew", "opengl32", "comctl32" }
        defines { "LIMINAL_DLL" }
        libdirs {
            -- "build/src/soil/bin"
        }
        postbuildcommands {
            commandline{
                mcpp_windows_args,
                CORE_CDEF
            }
        }
    --]]
    
    --[[
    configuration { "macosx" }
        links { "glew", "nanovg", "stk", "tess2", "pthread", "sdl2"  }
        buildoptions { "-stdlib=libstdc++" }
        libdirs {
            "build/lib"
        }
        linkoptions {
            "-v",
            "-undefined dynamic_lookup",
            "-stdlib=libstdc++",
            "-framework IOKit -framework AppKit -framework CoreAudio -framework CoreFoundation -framework CoreMidi" }
        if HAS_OVRSDK then
            defines { 'OVR_OS_MAC' }
        end
        postbuildcommands {
            commandline{
                mcpp_macosx_args,
                CORE_CDEF
            }
        }
    --]]
    
    configuration "debug"
        defines { "SCOPES_DEBUG" }
        flags { "Symbols" }

        buildoptions {
            "-O0"
        }
    
    configuration "release"
        defines { "NDEBUG" }
        flags { "Optimize" }
    
    
