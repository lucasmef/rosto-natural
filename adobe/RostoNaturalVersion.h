#ifndef ROSTO_NATURAL_VERSION_H
#define ROSTO_NATURAL_VERSION_H

// Keep the host-reported version and the PiPL resource byte-for-byte equal.
#define ROSTO_VERSION_MAJOR 0
#define ROSTO_VERSION_MINOR 11
#define ROSTO_VERSION_BUGFIX 0
#define ROSTO_VERSION_STAGE PF_Stage_RELEASE
#define ROSTO_VERSION_BUILD 1

#ifdef Rez
// Rez cannot parse AE_Effect.h. This is PF_VERSION(0, 11, 0, 3, 1).
#define ROSTO_EFFECT_VERSION 361985
#else
#define ROSTO_EFFECT_VERSION PF_VERSION( \
    ROSTO_VERSION_MAJOR, ROSTO_VERSION_MINOR, ROSTO_VERSION_BUGFIX, \
    ROSTO_VERSION_STAGE, ROSTO_VERSION_BUILD)
static_assert(ROSTO_EFFECT_VERSION == 361985,
              "Update the Rez version literal when changing the effect version");
#endif

#endif
