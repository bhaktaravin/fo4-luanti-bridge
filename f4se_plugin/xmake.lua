-- FO4LuantiBridge build (XMake). Normally built by GitHub Actions, see
-- .github/workflows/build-plugin.yml. CommonLibF4 lives in lib/commonlibf4.
set_xmakever("3.0.0")

includes("lib/commonlibf4")

set_project("FO4LuantiBridge")
set_version("0.1.0")
set_license("MIT")

set_languages("c++23")
set_warnings("allextra")
add_rules("mode.debug", "mode.releasedbg")

add_requires("cpp-httplib", "nlohmann_json")

-- Networking + INI reading, isolated from CommonLibF4's headers.
target("bridgenet")
    set_kind("static")
    set_arch("x64")
    add_files("net/net.cpp")
    add_headerfiles("net/net.h")
    add_includedirs("net", { public = true })
    add_packages("cpp-httplib", "nlohmann_json")
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
    add_syslinks("ws2_32", "crypt32")

-- The F4SE plugin DLL.
target("FO4LuantiBridge")
    add_rules("commonlibf4.plugin", {
        name = "FO4LuantiBridge",
        author = "ravin",
        description = "Syncs Fallout 4 with Luanti through bridge.py"
    })
    add_deps("bridgenet")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
