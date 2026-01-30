#pragma once

#include "rfal_default_config.h"

/* Enable listen (card emulation) for NFC-A */
#undef RFAL_SUPPORT_MODE_LISTEN_NFCA
#define RFAL_SUPPORT_MODE_LISTEN_NFCA true
