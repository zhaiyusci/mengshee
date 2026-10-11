# Canonical local development runtime

The agreed Mengshee development entry point on this machine is:

`C:\Users\jairy\Documents\okular\windows_build\dist\mengshee-pdf\app\bin\mengshee.exe`

The CMake build directory, `windows_build\build\mengshee-standalone`, is separate from this runtime directory. A successful build or verification in the build or temporary test directory **does not mean the development runtime has been updated**. When investigating a missing development entry point, do not start by assuming that the user opened the wrong version.

After installing a release, the user may instead be running `C:\Program Files\Mengshee\bin\mengshee.exe`. Check the actual process path. Synchronizing the development directory does not update the installed application, and processes with unsaved documents must not be closed.

## The development runtime must run with normal rights

The staged runtime — and the installed tree it is mirrored from — is meant to be started by a developer, so it must run at the integrity level of an ordinary process. Windows starts a process at the **lower** of the user's integrity level and the **executable's own label**, and build output created inside a restricted (low integrity) session inherits a low label from that session. An application started from such a tree looks healthy but runs with reduced rights:

- saving fails with **"could not open the temporary file for saving"**, because `part/part.cpp` needs a staging file in `%TEMP%`;
- KIO reports **"could not create socket for launching a KIO worker for protocol file"**, because its worker launch and runtime directory cannot be created;
- per-user files are redirected to `AppData\LocalLow` instead of `AppData\Local`.

This is a property of the tree, not of the application, so it must be fixed on the tree. The Windows pipeline does that itself now: `mengshee_prepare_run_tree()` (in `windows-build/cmake/run-tree-common.cmake`) resets the label of the installed tree and of the stage to Medium, reads the labels back, and **fails the build** when it cannot — naming the exact command. `windows-build/scripts/make-runnable.ps1` does the same for an existing tree, so a tree that was built before the check existed, or built with `-DALLOW_LOW_INTEGRITY=ON`, can be repaired without a rebuild:

```powershell
powershell -ExecutionPolicy Bypass -File windows-build\scripts\make-runnable.ps1 -Root <tree>
powershell -ExecutionPolicy Bypass -File windows-build\scripts\make-runnable.ps1 -Root <tree> -Check   # report only
```

Raising a label needs a normal (unelevated) session, so a build or shell that is itself restricted cannot repair the tree it produced; that is why the pipeline fails instead of shipping it. The staged smoke test checks the label too, so a runtime that would misbehave for whoever starts it does not pass validation. The installer is checked the same way, against its own `Setup.exe`: without a normal label it cannot unpack itself into `%TEMP%` and stops with `Setup was unable to create the directory "...\Temp\is-XXXX.tmp". Error 5: Access denied`, and a copy of it made inside a restricted session inherits the reduced label even when the source file was fine.

Do not work around this by pointing `%TEMP%` at a build directory: that only moves the staging file, leaves the KIO and per-user paths broken, and hides the real cause.

## Deployment and acceptance checks

- Confirm that the target application is closed; do not forcibly terminate user processes.
- Back up the existing files, then deploy matching application, Core, Part, and PDF backend binaries. Check any additional runtime dependencies required by the change rather than copying the entire SDK indiscriminately.
- The current core paths are `bin\Okular6Core.dll`, `bin\plugins\kf6\parts\okularpart.dll`, and `bin\plugins\okular_generators\okularGenerator_poppler.dll`.
- Compare deployed file hashes with the current build and validate using **the distribution's own libraries and plugins**. Do not let SDK entries in PATH conceal missing deployment dependencies.
- This package stores Qt platform plugins in `bin\platforms` and other plugins in `bin\plugins`. Include both the distribution's `bin` and `bin\plugins` directories in the plugin search path.
- Tests that initialize the LaTeX renderer must run beside the staged application DLLs, not from a separate test directory relying on `PATH`: renderer initialization enables Windows safe DLL search and registers the executable directory. After building the installer, temporarily copy the test executable and `Qt6Test.dll` into the stage's `bin`, run the checks, and remove those test-only files. Do not include them in the installer.
- When changing toolbar entry points, test the actual toolbar buttons. Checking that a QAction exists or triggering the action through a menu is not sufficient.
- Match regression scope to the change. Font hotfixes require focused font rendering/substitution tests, real-document comparisons, and packaged-runtime smoke tests—not unrelated mode-switch matrices, as explicitly requested by the user. The full mode matrix below applies to toolbar or mode-layout changes, not every hotfix.
- After changing the Poppler renderer, such as `SplashOutputDev.cc`, rebuild the SDK and deploy both `poppler.dll` and `poppler-qt6.dll`. Updating only the application's PDF plugin is insufficient.
- Starting with 2026.0.22.4, also deploy the 14 private Base14 fonts and their license/manifest files under `app\share\fonts`. These are mandatory core resources, not part of optional StemTeX. Validate the SDK, development runtime, and stage with `windows-build/cmake/pdf-base14-fonts.cmake`. See [PDF font rendering](pdf-font-rendering.md) for the font architecture and focused validation.
- `external/poppler/utils/PdfPageSequenceEditor.cc` is linked into the PDF backend as an application-side helper library. Changes to it require rebuilding `okularGenerator_poppler`; rebuilding only the SDK's `poppler.dll` does not update this code.

The reading-region editing mode is named **Reading Views**. Its views are regions within a page, not the traditional **View** display menu. Its mode-specific toolbar group is **right-aligned on the second row**, with **icon-only** buttons. Draw, Order, Generate, and Apply labels and descriptions belong in tooltips; Apply uses the icon showing one region copied to multiple regions. Annotation tools remain on the left. Acceptance checks must cover wide and narrow windows, all mode transitions, layout restoration, and XMLGUI reconstruction. Check the actual position of the final button, not merely whether the toolbar spans the row. Register the expanding spacer with XMLGUI and reapply the second-row placement and right-side ordering after reconstruction; do not rely on temporarily inserted widgets.
