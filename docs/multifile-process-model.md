# Multifile process prototype (Windows)

The `multifile` branch experiments with a coordinator and isolated document
processes. It is not a released application architecture yet.

## Ownership and failure boundaries

The normal launcher forwards file URLs and command-line options to a per-user,
per-executable coordinator. The coordinator starts one document process for each
file. Opening another file from an occupied document window also starts a separate
process. Each process owns its viewer, PDF backend and unsaved document state.
An auxiliary pane remains part of its owning document process.

The coordinator does not open PDFs or load StemTeX DLLs. It owns the shared
render queue and one `mengshee.exe --stemtex-service` host for the application's
global configuration. That host loads the renderer/converter DLLs and launches a
`stemtex-worker-host.exe` child, which launches XeTeX. A document process crash therefore does not
destroy another document's editing state. It can still lose its own unsaved work.
A coordinator crash interrupts outstanding typesetting requests, but surviving
document processes reconnect and start a replacement coordinator automatically.
Failed render requests are reported, not silently replayed.

The coordinator is started on demand, with no system service installation or
login autostart. It exits on an idle check (every ten seconds) once no documents
or typesetting requests remain. Normal shutdown asks each host to destroy its
renderer, allowing three seconds before forced cleanup. Each host and its
descendants belong to a dedicated Windows kill-on-close Job Object owned by the
coordinator. Documents are excluded. This also cleans up hosts and workers if
the coordinator crashes.

## Typesetting sharing

The configuration identity includes the runtime path, TeX tree, profile path,
profile preamble and warmup contents, and Mengshee's worker template. Documents
share one hot renderer. The profile is an application-wide preference, not a
document preference. Changing the configuration or restarting typesetting
replaces the single host; different profiles are never retained concurrently.
Document processes read persisted typesetting preferences for each request so a
window does not keep using a profile changed in another window. Native teardown
runs in the replaceable host, with a bounded grace period, rather than blocking
the coordinator inside a DLL destructor.

The local named pipe is restricted to the current user and scoped to this
Mengshee executable. Other applications using StemTeX are not enrolled in it.
This prototype serializes rendering and caps queued requests at 64. Closing a requesting
document does not cancel an active shared job. Startup, explicit reset and profile
replacement have a 60-second budget, including host replacement and warmup.
Ordinary typesetting, PDF conversion and any error/checkpoint recovery together
have a 10-second budget. A render needing a new host completes startup first,
then receives its separate 10-second render budget. No automatic retry extends
either deadline. A failed host is discarded so the next request can recover.
Queue waiting does not consume either execution budget. One-second progress
frames keep the client connection alive; 15 seconds without a coordinator reply
is a transport failure, not an execution timeout.
An explicit restart interrupts the active host request and invalidates requests
queued before the restart, so it does not wait behind a stuck native operation.
Request data and generated PDFs
cross the pipe; viewers retain ownership of their local result files.

## Development and verification

The dedicated developer runtime is under
`windows-build-workspace/install/mengshee-multifile/bin/mengshee.exe`.
`--new-process` retains the standalone development path. Standard input also
uses the standalone path. `--multifile-status` queries the running coordinator.
`MENGSHEE_MULTIFILE_INSTANCE` supplies a private namespace for integration tests.

`autotests/multifileprocesstest.py` requires Windows, Python with pypdf, a deployed
build, Qt's offscreen plugin, TeX Live and a configured StemTeX profile. Its
arguments are `--app`, `--profile`, `--texmf`, `--output` and `--qt-plugins`.
It creates its own namespace and terminates only its recorded test processes.
Checks cover separate document PIDs, a shared hot XeTeX, concurrent color and
geometry isolation, global profile replacement and repeated reset, TeX error recovery, document
process termination, coordinator termination/reconnection, and idle shutdown.
It also deliberately freezes the isolated renderer host during an active request
and verifies that reset interrupts that request and restores rendering, then
terminates a renderer host to check error reporting and recovery on the next
request. Recorded worker PIDs are checked for cleanup after reset and shutdown.

## Remaining prototype scope

The windows are independent top-level windows, not embedded child windows or a
central tab UI. There is no global unsaved-document recovery service. Shared UI
preferences and recent-file writes retain their existing behavior and still need
review for concurrent updates. IPC transport uses buffered JSON/base64; throughput
and large-result memory usage have not been benchmarked. Non-Windows launch
behavior is unchanged. Manual editing and prolonged use remain necessary before
considering a release.
