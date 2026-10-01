set_project("nuxe")
set_version("0.1.0")
set_languages("c++23")
set_toolchains("clang")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", { outputdir = ".", lsp = "clangd" })

add_requires("cli11 2.7.2", { system = false, configs = { cmake = false } })
-- Pin the upstream module until it is available in the standard Xmake package.
package("nlohmann_json_modules")
    set_kind("library", { headeronly = true })
    set_homepage("https://github.com/nlohmann/json")
    set_description("JSON for Modern C++ with its official module")
    set_license("MIT")
    add_urls("https://codeload.github.com/nlohmann/json/tar.gz/0c4462676dfe0787ed6788079821323a40416234",
             { filename = "nlohmann-json.tar.gz" })
    add_versions("3.12.0", "0da95ba36770c301541e12005db4a8a843d9fe1c450f63e70be4c496122e6ddc")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            target("nlohmann_json_modules")
                set_kind("moduleonly")
                set_languages("c++23")
                add_headerfiles("include/(nlohmann/**.hpp)")
                add_includedirs("include")
                add_files("src/modules/json.cppm", { public = true })
        ]])
        import("package.tools.xmake").install(package)
    end)
package_end()

add_requires("nlohmann_json_modules 3.12.0", { system = false })

set_warnings("all", "extra")
add_cxxflags("-Wpedantic", "-Wconversion", "-Wshadow", { tools = { "gcc", "clang" } })

target("nuxe")
    set_kind("binary")
    add_packages("cli11", "nlohmann_json_modules")
    add_files("src/**.cppm", { public = true })
    add_files("src/**.cpp")
    add_includedirs("include")
    add_headerfiles("include/(nuxe/*.h)")
