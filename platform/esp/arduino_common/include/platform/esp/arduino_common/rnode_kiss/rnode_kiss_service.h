#pragma once

#include "hostlink/hostlink_session.h"

namespace rnode_kiss
{

void start();
void stop();
bool is_active();
hostlink::Status get_status();

} // namespace rnode_kiss
