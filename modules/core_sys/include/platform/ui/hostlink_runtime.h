#pragma once

#include "hostlink/hostlink_session.h"

namespace platform::ui::hostlink
{

using LinkState = ::hostlink::LinkState;
using Status = ::hostlink::Status;

bool is_supported();
void start();
void stop();
bool is_active();
Status get_status();

} // namespace platform::ui::hostlink
