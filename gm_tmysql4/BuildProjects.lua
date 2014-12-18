--local boost = "/var/srcsdk/boost_1_57_0"
local boost = "D:/Repositories/boost_1_57_0"

solution "gm_tmysql4"

	language "C++"
	location ( os.get() .."-".. _ACTION )
	flags { "Symbols", "NoEditAndContinue", "NoPCH", "StaticRuntime", "EnableSSE" }
	targetdir ( "lib/" .. os.get() .. "/" )
	includedirs { "include/GarrysMod", "include/" .. os.get(), boost }
	platforms{ "x32" }
	libdirs { "library/" .. os.get(), boost .. "/stage/lib" }
	if os.get() == "windows" then
		links { "libmysql" }
	elseif os.get() == "linux" then
		links { "mysqlclient", "boost_date_time", "boost_regex", "boost_system" }
	else error( "unknown os: " .. os.get() ) end
	
	configurations
	{ 
		"Release"
	}
	
	configuration "Release"
		defines { "NDEBUG" }
		flags{ "Optimize", "FloatFast" }
	
	project "gm_tmysql4"
		defines { "GMMODULE" }
		files { "src/**.*", "include/**.*" }
		kind "SharedLib"
		