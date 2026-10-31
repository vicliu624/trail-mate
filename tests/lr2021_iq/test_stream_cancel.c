// Reuse the pinned upstream fake HAL, while linking the adapted production RX
// target. Keep the upstream source and its existing regression suite intact.
#define main upstream_radio_main
#include TRAIL_MATE_UPSTREAM_RADIO_TEST
#undef main

typedef struct
{
    unsigned calls, after;
} cancellation;
static bool requested(void* context)
{
    cancellation* request = context;
    return ++request->calls >= request->after;
}
int main(void)
{
    fake f;
    lr2021_iq_result r;
    static uint8_t staging[1280];
    for (unsigned scenario = 0; scenario < 6; scenario++)
    {
        lr2021_iq_hal h = init(&f);
        f.level = scenario == 0 ? 0 : 64;
        if (scenario == 2) f.reset_failure = -41;
        if (scenario == 3) f.restore_failure = -42;
        if (scenario == 4) f.flags = 0x10;
        cancellation request = {0, scenario == 5 ? 20u : 4u};
#if defined(TRAIL_MATE_TEST_ESP_STREAM)
        request.after = (request.after - 1) * 2 + 1;
#endif
        lr2021_iq_session session = {0};
        session.fifo_capacity = 1024;
        session.buffer = staging;
        session.buffer_size = sizeof(staging);
        session.cancelled = requested;
        session.cancel_context = &request;
        int code = lr2021_iq_stream_session(&h, &session, checked_sink, &f, &r);
        CHECK(code == (scenario == 2 ? -41 : scenario == 3 ? -42
                                         : scenario == 4   ? LR2021_IQ_DATA
                                                           : 0));
        CHECK(r.cancelled && !r.stopped_by_sink);
        CHECK(r.capture_error == (scenario == 4 ? LR2021_IQ_DATA : 0));
        CHECK(request.calls >= request.after && request.calls <= request.after + 1);
        CHECK(f.delivered == (scenario == 5 ? 1024u : 0u));
        CHECK(r.validated_bytes == f.delivered);
        CHECK(f.reset_count == 1 && f.restored == (scenario == 2 ? 0u : 1u));
        CHECK(!f.locked && !f.fast);
        CHECK(r.bytes == (scenario == 0 ? 0u : scenario == 5 ? 1216u
                                                             : 192u));
        if (scenario < 2) CHECK(f.active);
    }
    // Host legacy APIs keep their fallback; ESP sessions must supply storage.
#if defined(TRAIL_MATE_TEST_ESP_STREAM)
    lr2021_iq_hal h = init(&f);
    f.level = 64;
    cancellation during_wait = {0, 2};
    lr2021_iq_session paced = {0};
    paced.fifo_capacity = 1024;
    paced.buffer = staging;
    paced.buffer_size = sizeof(staging);
    paced.cancelled = requested;
    paced.cancel_context = &during_wait;
    CHECK(!lr2021_iq_stream_session(&h, &paced, checked_sink, &f, &r));
    CHECK(r.cancelled && !r.bytes && !f.delivered && f.reset_count == 1);
    h = init(&f);
    lr2021_iq_session invalid = {0};
    invalid.fifo_capacity = 1024;
    CHECK(lr2021_iq_stream_session(&h, &invalid, checked_sink, &f, &r) == LR2021_IQ_ARGUMENT);
    CHECK(!f.locked && !f.reset_count && !f.delivered);
    CHECK(lr2021_iq_stream_checked(&h, 10, checked_sink, &f, &r) == LR2021_IQ_ARGUMENT);
#else
    CHECK(!upstream_radio_main());
#endif
    puts("IQ cancellation: empty FIFO, pending tail and recovery failures passed");
    return 0;
}
