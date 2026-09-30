# addc-base (vendored)

Snapshot of `AMD-DCTOOLS/addc-base` at commit `9a9675f` (version 3.3.0), used only by
`libamd_smi` to turn a CPER record into AFIDs and the JSON event report.

Copied from the upstream tree: `src/{addc-common,addc-cper-parser,addc-cper-pipeline,addc-decoder,addc-mca,api,core,products,tier_api}`,
`include/`, `LICENSE` and `meson.build` (reference only). The tests, fuzz targets, CLI and manifest are not copied.
`CMakeLists.txt` in this directory is amdsmi's own build of those sources; it is not upstream's.

To update: run `git -C <addc-base> archive <commit> <the paths above> | tar -x -C third_party/addc-base`,
update the commit in the comment at the top of `CMakeLists.txt`, and make `ADDC_SOURCES` match the
sources and definitions in the new `meson.build`. The configure fails if the `.cpp` files and the list disagree.
