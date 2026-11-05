#pragma once

// Identity of the plug-in. Keep the FUIDs as they are: hosts use them to find the plug-in and restore
// saved projects.

#define LOOPTEXTURER_NAME "LoopTexturer"
#define LOOPTEXTURER_VENDOR "Hadronyche"
#define LOOPTEXTURER_URL ""
#define LOOPTEXTURER_EMAIL ""

// Class IDs, passed to Steinberg::FUID (a, b, c, d)
#define LOOPTEXTURER_PROCESSOR_UID 0x2402746B, 0x3F884603, 0x87927853, 0x1D2D1EF1
#define LOOPTEXTURER_CONTROLLER_UID 0xE906F66D, 0x91354D4F, 0x8BEBAFFE, 0x709D2759
