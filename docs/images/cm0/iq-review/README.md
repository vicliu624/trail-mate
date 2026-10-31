# IQ diagnostic layout fixtures

These PNGs are native SDL renders of the production shared Protocol Probe page
at 320x170 and 1232x568. The IQ port is explicitly replaced by fixed test data in
`apps/linux_lvgl/tests/linux_probe_session_page_test.cpp`; every image is marked
`TEST FIXTURE - NO RF`. They demonstrate layout and error presentation only.
They are not device captures, RF measurements or evidence of successful reception.

The test temporarily marks the diagnostic heading before export and waits for
native control transitions to settle. No image elements are painted afterward.
BMP pixels are converted to PNG without resizing or changing content.

To reproduce from a configured Linux LVGL build:

```sh
cmake --build <build> --target trailmate_linux_probe_iq_page_test
TRAIL_MATE_IQ_FIXTURE_SCREENSHOT_DIR=<output-directory> \
  ctest --test-dir <build> -R '^linux_probe_iq_page(_compact)?$' --output-on-failure
```

`compact-iq-ready.png` shows the four-line summary. `compact-iq-error.png` shows
a long error constrained to the scroll viewport above the footer. Wide variants
show the same production widgets without small-screen compression. The tests
also verify that Down reaches the diagnostic tail, a new capture returns to the
top, and controls become available after recovery.
