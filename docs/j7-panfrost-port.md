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

The service uses one address space and one global in-flight job. Its current
client self-test submits Mali WRITE_VALUE jobs. Patch `0313` now adds a
candidate Midgard v5 one-tile fragment clear into a private 16x16 RGBA target
and counts the resulting pixels. This is a boot-time driver diagnostic, not a
screen acceleration path; it is not yet XNU-built or hardware-verified. The
service still does not create CoreSurface-backed buffers or expose DRM ioctls.

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
`J7MaliUserClient`, correct cache ownership around IOSurface buffers, and
on-device verification that patch `0313` produces all expected pixels. Only
after that passes should a surface be presented to the Samsung scanout buffer.

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
