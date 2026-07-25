#ifndef KARTGAME_VERSION_H
#define KARTGAME_VERSION_H

// Version lives in the repository, not in the build command, so that a binary's
// reported version is a property of the source it was built from.
#define KARTGAME_VERSION "0.1.0-m0"

// Stringify a macro's expansion. Used for build facts that SCons passes in as
// bare tokens (-DKARTGAME_BUILD_TARGET=editor), which avoids fighting three
// different shells over nested quotes.
#define KARTGAME_STRINGIFY_(x) #x
#define KARTGAME_STRINGIFY(x) KARTGAME_STRINGIFY_(x)

#endif // KARTGAME_VERSION_H
