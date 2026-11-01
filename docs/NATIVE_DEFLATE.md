# Retail guest deflate interception

The decrypted 1.09 executable loads at `0x800000000`. Its zlib 1.2.3 `inflate`
entry is image-relative `0x2067ac0`; the DCX reader calls it at `0x2625935`.
It accepts `(z_stream*, int flush)` under System V x86-64. A four-argument
`(dst, capacity, src, length)` replacement would corrupt that ABI.

`hooks_decomp.cpp` verifies 20 entry bytes, moves six complete non-relative
instructions into a guest stub, and installs a rel32 redirect. The original
trampoline remains callable. Signature mismatch leaves the game unchanged.
`BB_NATIVE_DEFLATE=0` disables installation.

`guest_deflate.cpp` handles only initial complete zlib streams: window size 15,
no dictionary, no buffered bits and zero input/output totals. Each input and
output capacity is at most 8 MiB. Raw/gzip, dictionaries, incremental streams,
unsupported flush modes, overlapping or invalid ranges and failed allocations
return to the original routine. libdeflate is not a streaming substitute for
zlib; broadening this path without preserving guest stream state would be wrong.

A per-thread RAII decoder owns libdeflate's small decoding context. Per-call
scratch makes failed decode transactional and is freed before returning. Output
commits through `runtime_memory_write_backing_rows`, including its VMA transaction
and preflight, with runtime write notifications. Only successful output commits
update guest pointers, counts and checksum. The private guest state uses zlib
1.2.3's `DONE = 26`, verified against the original executable rather than the
host zlib enum. This remains synchronous on the calling guest thread; it does
not parallelize dependent streaming calls or move them to E cores.

The unit test compares output and state against host zlib and verifies corrupt,
truncated, undersized, overlapping and unsupported inputs leave the stream and
output unchanged. An optional private-asset test maps original guest code and
calls its original init/inflate/end routines:

```sh
out/gpu/guest-deflate-test /path/to/decrypted/eboot.elf /path/to/CUSA03173/dvdroot_ps4
```

The test supplies only allocator/free/memcpy and stack-canary imports; the
original guest decompressor runs unchanged. Native terminal state must also be
accepted by that original guest routine. The executable and DCX assets are not
stored in Git. Final measured results and limitations are in
[the consolidated audit](CONVERGENCE_AUDIT_FINAL.md).
