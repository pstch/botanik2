# Integration manual {#int}

This section of the documentation describes how one case the Botanik runtime and library to 
define a flashable PLC firmware for a given "site". Botanik is not solely a library since
it uses codegen to be extensible by the sites.

It's possible to develop a firmware directly in the Botanik repository, but it's not recommended.
The recommended way to develop a firmware is to give it its own repository, with `botanik` included
as a Git submodule, define a `CMakeLists.txt` that includes a CMake preamble and calls `botanik_add_site`
with the proper parameters. Then, the site can be described using :
 - a TOML file for project-level settings
 - CSV tables for all kinds of PLC variables
 - ST control programs

When building, the CMake code in `botanik` will then be called to execute the code generation which will
generate various C/C++ files from these declarations. The base library also provides a "test kit" that
facilitates writing tests in site-level repositories.

`botanik` provides examples of site-level repositories :
 - `examples/ref` : a very simple reference site, with two variables and a control program
 - `examples/nosite` : an example where botanik is used solely as a library, in this case for its `Console` component
 - `examples/gh_mlb41` : a complete production site, included as a Git submodule

The following pages describe in more detail how to create a control system based on `botanik` :

- @subpage int_config
- @subpage int_model
- @subpage int_modbus
- @subpage int_mqtt
- @subpage int_fieldbus
- @subpage int_site
- @subpage int_st
- @subpage int_tasks
- @subpage int_extending
