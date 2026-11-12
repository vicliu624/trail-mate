#include "platform/esp/arduino_common/hostlink/hostlink_download_mode.h"
#include <cassert>

int main()
{
    namespace download = hostlink::download_mode;
    assert(!download::supported() && !download::active());
    assert(download::request({}, 1) == hostlink::management::Status::Unsupported);
    download::service();
    download::reply_completed(1, true);
    assert(!download::active());
}
