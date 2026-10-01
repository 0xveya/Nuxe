set_project("nuxe")
set_version("0.1.0")
set_languages("c++23")
set_toolchains("clang")

add_rules("mode.debug", "mode.release")

set_warnings("all", "extra")
add_cxxflags("-Wpedantic", "-Wconversion", "-Wshadow", { tools = { "gcc", "clang" } })

target("nuxe")
    set_kind("binary")
    add_files("src/**.cppm", { public = true })
    add_files("src/**.cpp")
    add_includedirs("include")
    add_headerfiles("include/(nuxe/*.h)")
