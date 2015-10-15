local boost = "/home/jake/Developer/boost_1_57_0"

solution "gm_tmysql4"

	language "C++"
	location ( os.get() .."-".. _ACTION )
	flags { "Symbols", "NoEditAndContinue", "NoPCH", "StaticRuntime", "EnableSSE" }
	targetdir ( "lib/" .. os.get() .. "/" )
	includedirs { "include/GarrysMod", "include/mysql", boost } 
	platforms{ "x32" }
	libdirs { "library/" .. os.get(), boost .. "/stage/lib" }
	if os.get() == "windows" then
		links { "libmysql" }
	elseif os.get() == "linux" then
		links { "mysqlclient", "boost_system" }
	else error( "unknown os: " .. os.get() ) end
	
	configurations
	{ 
		"Release"
	}
	
	configuration "Release"
		buildoptions { "-std=c++11 -Wno-deprecated-declarations -pthread -Wl,-z,defs" }
		defines { "NDEBUG" }
		flags{ "Optimize", "FloatFast" }
	
	project "gm_tmysql4"
		defines { "GMMODULE" }
		files { "src/**.*", "include/**.*" }
		kind "SharedLib"
		
