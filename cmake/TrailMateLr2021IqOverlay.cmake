include_guard(GLOBAL)

# Local RX-only adaptation of the pinned MIT source. Never modify a shared
# FetchContent checkout. Exact hashes make upstream changes require review.
function(trailmate_prepare_lr2021_iq_overlay source_dir output_dir)
    file(SHA256 "${source_dir}/src/lr2021_iq.c" source_hash)
    file(SHA256 "${source_dir}/include/lr2021_iq.h" header_hash)
    if(NOT source_hash STREQUAL "1574ad6bec8708c68b0e9d018f25d85fef2ecec36dafcf550f908ec79c839127" OR
       NOT header_hash STREQUAL "ec07038acda97b7420702b01b55107413bbc9c811f69e15fd014c61527705e20")
        message(FATAL_ERROR "LR2021 RX overlay: pinned source changed; review cancellation adaptation")
    endif()
    file(READ "${source_dir}/src/lr2021_iq.c" rx_source)
    file(READ "${source_dir}/include/lr2021_iq.h" rx_header)
    # ESP tasks must not reserve the upstream 1280-byte fallback on their stack,
    # even when a caller-owned buffer is supplied. Host legacy APIs keep their
    # fallback; ESP continuous capture requires the checked session API.
    string(REPLACE "    uint8_t local[1280];\n    uint8_t *pending=local;\n    size_t cap=sizeof(local), chunk=256, check=1024;"
        "#if defined(ESP_PLATFORM)\n    if(!checked || !session || !session->buffer)return LR2021_IQ_ARGUMENT;\n    uint8_t *pending=session->buffer;\n    size_t cap=session->buffer_size, chunk=256, check=1024;\n#else\n    uint8_t local[1280];\n    uint8_t *pending=local;\n    size_t cap=sizeof(local), chunk=256, check=1024;\n#endif" rx_source "${rx_source}")
    # Unchecked host streaming can reuse its fallback staging area. Checked
    # streams already write directly there; no second automatic array is needed.
    string(REPLACE "            uint8_t data[256];" "            uint8_t *data=pending;" rx_source "${rx_source}")
    string(REPLACE "(default: 1280 on the stack)." "(host default: 1280 on the stack; ESP requires caller storage)." rx_header "${rx_header}")
    string(REPLACE "    unsigned gain_changes;" "    unsigned gain_changes;\n    /* Trail Mate: explicit cancellation, independent of sink delivery. */\n    bool cancelled;" rx_header "${rx_header}")
    string(REPLACE "    unsigned level_every;" "    unsigned level_every;\n    /* Bounded, nonblocking; no radio calls. Null preserves upstream behavior. */\n    bool (*cancelled)(void *);\n    void *cancel_context;" rx_header "${rx_header}")
    string(REPLACE "        uint64_t poll_time=h->now_us(c), gap=poll_time-previous_poll;"
        "        if(session && session->cancelled && session->cancelled(session->cancel_context)) {\n            r->cancelled=true; break;\n        }\n#if defined(ESP_PLATFORM)\n        /* Give the dedicated producer a sub-tick blocking service interval. */\n        h->sleep_us(c,100);\n        if(session && session->cancelled && session->cancelled(session->cancel_context)) {\n            r->cancelled=true; break;\n        }\n#endif\n        uint64_t poll_time=h->now_us(c), gap=poll_time-previous_poll;" rx_source "${rx_source}")
    string(REPLACE "    r->elapsed_us=h->now_us(c)-start;\n    if(touched) {"
        "    if(session && session->cancelled && session->cancelled(session->cancel_context))r->cancelled=true;\n    r->elapsed_us=h->now_us(c)-start;\n    if(touched) {" rx_source "${rx_source}")
    string(REPLACE "if(checked && !e && !f && !(flags[0]&0x30) && pending_bytes)"
        "if(checked && !r->cancelled && !e && !f && !(flags[0]&0x30) && pending_bytes)" rx_source "${rx_source}")
    string(REPLACE "if(!e && !r->bytes)e=LR2021_IQ_TIMEOUT;"
        "if(!e && !r->bytes && !r->cancelled)e=LR2021_IQ_TIMEOUT;" rx_source "${rx_source}")
    file(MAKE_DIRECTORY "${output_dir}/include")
    # configure_file avoids touching generated timestamps on unchanged reconfigure.
    file(CONFIGURE OUTPUT "${output_dir}/lr2021_iq.c" CONTENT "${rx_source}" @ONLY)
    file(CONFIGURE OUTPUT "${output_dir}/include/lr2021_iq.h" CONTENT "${rx_header}" @ONLY)
endfunction()
