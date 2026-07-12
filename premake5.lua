-- main solution file.
workspace "MetalPlayground"
    architecture "ARM64"

    configurations 
    { 
        "Debug",
        "Release" 
    }

    startproject "MetalApp"

-- Variable to hold output directory.
outputdir = "%{cfg.buildcfg}-%{cfg.architecture}"

-- Engine project.
project "MetalEngine"
    location "MetalEngine"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "On"
    multiprocessorcompile "On"

    -- Directories for binary and intermediate files.
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files 
    { 
        "%{prj.name}/src/**.h", 
        "%{prj.name}/src/**.cpp",
        "%{prj.name}/src/**.mm",
    }

    defines
	{
		"_CRT_SECURE_NO_WARNINGS",
	}

    -- Include directories.
    externalincludedirs
    {
        "Dependencies",
        "Dependencies/GLFW/include",
        "Dependencies/Metal-Cpp",
        "Dependencies/GLM",
    }

    libdirs
    {
        "Dependencies/GLFW/glfw-3.4.bin.MACOS/lib-arm64",
    }

    links
    {
        "glfw3",

        "Cocoa.framework",
        "IOKit.framework",
        "CoreVideo.framework",
        "QuartzCore.framework",
        "Metal.framework",
        "Foundation.framework"
    }

    filter "system:macosx"
        systemversion "latest"

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "RELEASE" }
        optimize "On"

-- App project.
project "MetalApp"
    location "MetalApp"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "On"
    multiprocessorcompile "On"

    -- Directories for binary and intermediate files.
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files 
    { 
        "%{prj.name}/src/**.h", 
        "%{prj.name}/src/**.cpp"
    }

    defines
	{
		"_CRT_SECURE_NO_WARNINGS",
	}

    -- Include directories.
    includedirs
    {
        "MetalEngine/src",
    }

    externalincludedirs
    {
        "Dependencies",
        "Dependencies/GLFW/include",
        "Dependencies/Metal-Cpp",
        "Dependencies/GLM",
    }

    libdirs
    {
        "Dependencies/GLFW/glfw-3.4.bin.MACOS/lib-arm64",
    }

    links
    {
        "MetalEngine",

        "glfw3",

        "Cocoa.framework",
        "IOKit.framework",
        "CoreVideo.framework",
        "QuartzCore.framework",
        "Metal.framework",
        "Foundation.framework"
    }

    filter "system:macosx"
        systemversion "latest"

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "RELEASE" }
        optimize "On"