
local THISDIR = os.getcwd()
local CLANG_PATH
local MSYS_BASE_PATH = "C:/msys64"
local MINGW_BASE_PATH = MSYS_BASE_PATH .. "/mingw64"
local MSYS_BIN_PATH = MSYS_BASE_PATH .. "/usr/bin"
if os.is("linux") then
    CLANG_PATH = THISDIR .. "/clang/bin:/usr/local/bin:/usr/bin"
elseif os.is("windows") then
    CLANG_PATH = MINGW_BASE_PATH .. "/bin"
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
    --print(cmd, "=>", args)
    return flatten(string.explode(args, "[ \n]+"))
end

local function finddir(name, searchpath)
    searchpath = searchpath or CLANG_PATH
    local path = os.pathsearch(name, searchpath)
    assert(path, name .. " not found in path " .. searchpath)
    return path .. "/" .. name
end

local function dllpath(name, searchpath)
    searchpath = searchpath or CLANG_PATH
    assert(os.is("windows"))
    name = name .. ".dll"
    local path = os.pathsearch(name, searchpath)
    assert(path, name .. " not found in path " .. searchpath)
    return path .. "/" .. name
end

local function toolpath(name, searchpath)
    searchpath = searchpath or CLANG_PATH
    if os.is("windows") then
        name = name .. ".exe"
    end
    local path = os.pathsearch(name, searchpath)
    assert(path, name .. " not found in path " .. searchpath)
    return path .. "/" .. name
end

local function print_list(l)
    for k,v in pairs(l) do
        print(k,v)
    end
end

local CLANG_CXX = toolpath("clang++", CLANG_PATH)
local LLVM_CONFIG = toolpath("llvm-config", CLANG_PATH)

local LLVM_LDFLAGS = pkg_config(LLVM_CONFIG .. " --ldflags")
local LLVM_CXXFLAGS = pkg_config(LLVM_CONFIG .. " --cxxflags")
local LLVM_LIBS = pkg_config(LLVM_CONFIG .. " --libs engine passes option objcarcopts coverage support lto coroutines")

premake.gcc.cxx = CLANG_CXX
if os.is("windows") then
    local CLANG_CC = toolpath("clang", CLANG_PATH)
    premake.gcc.cc = CLANG_CC
end
premake.gcc.llvm = true

solution "scopes"
    location "build"
    configurations { "debug", "release" }
    platforms { "native", "x64" }

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

    buildoptions_cpp {
        "-std=c++11",
        "-fno-rtti",
        "-fno-exceptions",
        "-ferror-limit=1",
        "-pedantic",
        "-Wall",
        "-Wno-keyword-macro",
    }

    buildoptions_cpp(LLVM_CXXFLAGS)

    links {
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

    configuration { "linux" }
        defines {
            "_GLIBCXX_USE_CXX11_ABI=0",
        }

        links { 
            "pthread", "m", "tinfo", "dl", "z",
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
    
    configuration { "windows" }
        buildoptions_cpp {
            "-Wno-unknown-warning-option",
            "-Wno-unused-variable",
            "-Wno-unused-function",        
        }
    
        includedirs {
            "win32",
            MINGW_BASE_PATH .. "/lib/libffi-3.2.1/include"
        }
    
        files {
            "win32/mman.c",            
            "win32/realpath.c",
            "win32/dlfcn.c",
        }

        defines {
            "SCOPES_WIN32",
        }

        buildoptions_c {
            "-Wno-shift-count-overflow"
        }

        links { 
            "ffi", "ole32", "uuid", "version", "psapi"
        }

        linkoptions { "-Wl,--allow-multiple-definition" }
        linkoptions(LLVM_LDFLAGS)
        linkoptions { "-Wl,--whole-archive", "-Wl,--export-all-symbols" }
        linkoptions(LLVM_LIBS)
        linkoptions { "-Wl,--no-whole-archive" }
        
        if os.is("windows") then
            local CP = toolpath("cp", MSYS_BIN_PATH)

            postbuildcommands {
                CP .. " -v " .. THISDIR .. "/bin/scopes " .. THISDIR,
                CP .. " -v " .. dllpath("libffi-6") .. " " .. THISDIR,
                CP .. " -v " .. dllpath("libgcc_s_seh-1") .. " " .. THISDIR,
                CP .. " -v " .. dllpath("libstdc++-6") .. " " .. THISDIR,
                CP .. " -v " .. dllpath("libwinpthread-1") .. " " .. THISDIR,
                CP .. " -v " .. dllpath("LLVM") .. " " .. THISDIR,
            }
        end
    
    configuration "debug"
        defines { "SCOPES_DEBUG" }
        flags { "Symbols" }

        buildoptions {
            "-O0"
        }
    
    configuration "release"
        defines { "NDEBUG" }
        flags { "Optimize" }
    
    
