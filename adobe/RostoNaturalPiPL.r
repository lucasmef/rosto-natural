#include "AEConfig.h"
#include "AE_EffectVers.h"
#ifndef AE_OS_WIN
#include "AE_General.r"
#endif
#include "RostoNaturalVersion.h"

// Keep the PiPL resource source compatible with Rez. Adobe's effect samples
// do not include the C API headers here; mirror the SDK value from AE_Effect.h.
#define PF_OutFlag_NON_PARAM_VARY 0x00000004

resource 'PiPL' (16000) {
  {
    Kind { AEEffect },
    Name { "Rosto Natural" },
    Category { "Lucas" },
    CodeMacARM64 { "EffectMain" },
    AE_PiPL_Version { 2, 0 },
    AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
    AE_Effect_Version { ROSTO_EFFECT_VERSION },
    AE_Effect_Info_Flags { 0 },
    AE_Effect_Global_OutFlags { PF_OutFlag_NON_PARAM_VARY },
    AE_Effect_Global_OutFlags_2 { 0 },
    AE_Effect_Match_Name { "com.lucas.RostoNatural" },
    AE_Reserved_Info { 0 },
    AE_Effect_Support_URL {
      "https://github.com/lucasmef/rosto-natural"
    }
  }
};
