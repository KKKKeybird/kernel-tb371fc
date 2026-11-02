# TB371FC HyperOS reviewed kernel snapshot #24

This branch includes the local audio, pen/keyboard, memory-extension and
touch-interface commits following the original GitHub main branch.

The final reviewed changes add the thermal_message compatibility interface,
the bounded mi_asap compatibility interface and the lz4kd default compressor.
The accepted build uses the full configuration in
`arch/arm64/configs/tb371fc_hyperos_reviewed24.config`.

To use the recorded configuration, copy it to `.config`, run the normal
olddefconfig step with the existing project toolchain, then follow the build
scripts. Existing CI defaults are not a claim of a newly verified CI build.

The previously built and device-tested Image SHA-256 was:
`337d2d42aa1f3c79ac48263e906ffcf92c1feb9dbbde1cc0b9be375642df8bd7`.
The packaged reviewed boot SHA-256 was:
`a0d3a11eb761dbe63d0175dd4ea597a3d26881c8a3ef264ca2b3fe42d886a5a9`.

This source publication does not flash or rebuild the device. Backup `.bak`
files, generated syscall tools/headers and generated headers_install output
are not part of the source change. Existing licenses and attribution remain
in the source tree.
