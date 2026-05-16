# Cardputer Zero Board Facts

Sources:

- `docs/LINUX_ADAPTATION_GUIDE.md`
- `docs/targets/linux_targets.md`
- `platform/linux/common/src/core/display_profile.h`

This record describes current repo evidence for the Linux Cardputer Zero route.

## Identity

- board id: `cardputerzero`
- current build entry evidence: `builds/linux_cmake`
- current implementation evidence: `removed root linux_rpi`
- current shared Linux code: `platform/linux/common`

## Confirmed Facts

- logical display size used by current common Linux shell: 320 x 170
- keyboard input is required but real device key mapping still needs sampling
- display/input ownership for the real Pi OS path is not yet closed
- current final shell is not proven as a dedicated app directory in this repo
