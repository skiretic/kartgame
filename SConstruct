#!/usr/bin/env python
"""Build the kartgame C++ GDExtension.

Usage:
    scons target=editor              # what the Godot editor loads
    scons target=template_release    # what an exported build loads
    scons target=editor arch=universal   # macOS fat binary, for releases

godot-cpp's own SConstruct owns the toolchain, the platform detection, and the
binding generation. This file only adds our sources on top of the environment it
hands back, so every option godot-cpp documents (`scons --help`) works here too.
"""

import os

# Sourcing godot-cpp's SConstruct both builds libgodot-cpp and returns a fully
# configured environment: compiler, flags, include paths, and the generated
# bindings. Anything we set before this call would be overwritten.
env = SConscript("third_party/godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])

# Build facts, passed as bare tokens and stringified in src/version.h. Reported by
# KartCore.build_info() so a running binary can say what it is.
env.Append(
    CPPDEFINES=[
        ("KARTGAME_BUILD_TARGET", env["target"]),
        ("KARTGAME_BUILD_PLATFORM", env["platform"]),
        ("KARTGAME_BUILD_ARCH", env["arch"]),
    ]
)


def collect_sources(root):
    """Every .cpp under root, recursively.

    Explicit recursion rather than a flat Glob so that adding src/vehicle/ or
    src/track/ in a later milestone needs no build-file edit — the directories in
    ARCHITECTURE.md §13 land as they are written.
    """
    found = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for filename in filenames:
            if filename.endswith(".cpp"):
                found.append(os.path.join(dirpath, filename))
    return sorted(found)


sources = collect_sources("src")

if not sources:
    print("ERROR: no C++ sources found under src/")
    Exit(1)

# macOS omits the architecture from the filename; every other platform keeps it.
#
# The reason is the fat binary: a universal build covers arm64 and x86_64 in one
# file, so there is no single architecture to name. Leaving it out means a local
# arm64 build and a CI universal build produce the same filename, and one line in
# kartgame.gdextension covers both.
if env["platform"] == "macos":
    library_name = "libkartgame.{}.{}{}".format(env["platform"], env["target"], env["SHLIBSUFFIX"])
else:
    library_name = "libkartgame{}{}".format(env["suffix"], env["SHLIBSUFFIX"])

library = env.SharedLibrary(os.path.join("bin", library_name), source=sources)

# The shared library is the one artifact that must never come from a stale cache —
# a wrong-target .dylib loads and then misbehaves rather than failing loudly.
env.NoCache(library)
Default(library)
