# J7 Mali-T830: Panfrost integration notes

## Hardware and Mesa support

The J7 Prime reports Mali-T830, product `0x0830`, from its XNU GPU probe. Mesa's
Panfrost driver supports T830 as a Midgard v5 GPU and exposes OpenGL ES; this
does not make Mesa usable on iOS by itself ([Panfrost hardware support](https://docs.mesa3d.org/drivers/panfrost.html)).
The pinned source examined for this port is Mesa 24.3.4, checkout
`769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de`, kept under ignored
`local-build/mesa-j7-24.3.4/` for local research.

## Existing XNU boundary

With `j7gpu=7`, `J7MaliUserClient` provides:

| Selector | Existing operation |
| --- | --- |
| 0 / 1 | Pin a caller-owned virtual range into Mali AS0; unmap it |
| 2 / 3 | Submit one job chain to a job slot; synchronously wait/recover |
| 4 | Return GPU ID and core/job/address-space masks |
| 5 | Send bounded text to the kernel log |
| 6 (`0315`) | Import a SWEET IOSurface by ID into AS0 and return its GPU VA and geometry |

The service uses one address space and one global in-flight job. Its current
client self-test submits Mali WRITE_VALUE jobs. Patch `0313` now adds a
candidate Midgard v5 one-tile fragment clear into a private 16x16 RGBA target
and counts the resulting pixels. This is a boot-time driver diagnostic, not a
screen acceleration path; it is not yet XNU-built or hardware-verified. The
surface-import selector in `0315` pins and maps an existing SWEET IOSurface's
pixel memory, retaining the surface until selector 1 unmaps it. This gives a
Panfrost winsys a zero-copy resource path for surfaces it can already access.
The selector does not expose DRM ioctls, implement buffer allocation/import in
Mesa, or route CoreAnimation rendering to Mali. Patch `0315` is a code
candidate; it is not XNU-built or hardware-verified.

## Mesa compatibility work identified

Mesa 24.3.4's `src/panfrost/lib/kmod/panfrost_kmod.c` expects DRM operations
for device properties, BO create/import/free, CPU mappings, GPU VA assignment,
and waits. `src/gallium/drivers/panfrost/pan_jm.c` submits job chains with BO
lists and sync objects. A first J7 adapter can use the existing IOKit selectors
to map anonymous user memory, assign returned Mali VAs, submit the job chain,
and wait synchronously. Mesa's panfrost properties can be filled from selector
4 plus the T830 model defaults from Mesa. It must disable unsupported sync-file
import/export, eviction, heap-on-fault, and concurrent jobs for this first
serialized path.

The integration still needs a Mesa winsys entry that talks to
`J7MaliUserClient`, maps/allocates BOs through the surface and memory selectors,
translates Panfrost's submit/wait path, and preserves cache ownership around
IOSurface buffers. The next hardware gate is `0313` producing all expected
pixels, followed by a render into an imported IOSurface and presentation of that
surface to Samsung scanout.

## UI limit

Panfrost is a GLES driver; SpringBoard's iOS 16 renderer expects Apple's
Metal/IOGPU stack. Loading a GLES library into `backboardd` does not make
CoreAnimation use it. A separate EAGL/OpenGL interception can accelerate only
the GLES call sites it actually receives. Replacing the SpringBoard renderer
requires an iOS graphics ABI bridge or proof that the target's renderer uses a
hookable GLES path. A Mali job test or faster framebuffer copy alone is not
evidence that the lock screen is GPU-rendered.

Patch `0314` is a separate CPU-side experiment: it reuses the HEROLTE row-band
copy worker for J7 scanout, scaling up to three helpers according to XNU's
active CPU count. It may reduce the final presentation copy cost, but leaves
the software renderer intact and has not been built or measured on the phone.
