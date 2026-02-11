#include "sstv_types.h"

const char* vis_mode_name(VisMode mode)
{
    switch (mode)
    {
    case VisMode::Scottie1:
        return "Scottie 1";
    case VisMode::Scottie2:
        return "Scottie 2";
    case VisMode::ScottieDX:
        return "Scottie DX";
    case VisMode::Robot72:
        return "Robot 72";
    case VisMode::Robot36:
        return "Robot 36";
    case VisMode::Martin1:
        return "Martin 1";
    case VisMode::Martin2:
        return "Martin 2";
    case VisMode::Pd50:
        return "PD50";
    case VisMode::Pd90:
        return "PD90";
    case VisMode::Pd120:
        return "PD120";
    case VisMode::Pd160:
        return "PD160";
    case VisMode::Pd180:
        return "PD180";
    case VisMode::Pd240:
        return "PD240";
    case VisMode::Pd290:
        return "PD290";
    case VisMode::P3:
        return "P3";
    case VisMode::P5:
        return "P5";
    case VisMode::P7:
        return "P7";
    default:
        return "Unknown";
    }
}
