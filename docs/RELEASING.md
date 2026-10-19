# Manual releases from pushed tags

Run the release workflow from the repository containing this source. Keep downloaded dependencies, toolchains, generated artifacts and credentials out of Git: `.deps/`, `.tools/`, `.venv/`, `build/`, `dist/`, and `private/` are ignored.

The release workflow is `.github/workflows/release.yml`. It runs **only through workflow_dispatch**. A tag push does not automatically release anything. GitHub's workflow-dispatch UI requires the workflow to exist on the default branch.

1. Commit the desired source and workflow.
2. Create a version tag and push it:

   ```sh
   git tag -s v0.1.0-rc.1 -m "First native firmware candidate"
   git push origin v0.1.0-rc.1
   ```

3. In GitHub, open **Actions → Manual firmware release → Run workflow**. Choose the default branch for the workflow, supply `v0.1.0-rc.1`, and keep the prerelease option enabled for hardware candidates.
4. Wait for both matrix builds, the secret scan and the draft job. Each build uses the exact resolved tag commit.
5. Open **Releases**, inspect the draft, and publish it manually after the intended acceptance checks.

Tags must look like `v1.2.3` or `v1.2.3-rc.1`. Lightweight and annotated tags are supported. Missing tags fail; the workflow never creates a tag. It checks again before release creation that the tag still points at the resolved commit. Existing releases are never overwritten. Use a new tag for a different image rather than moving an already released tag.

Each target is tested, built, and packaged independently on Ubuntu 24.04. The draft job requires both successful targets and the Gitleaks Git-history scan, validates matching manifests/reports/checksums, and uses its write-scoped token only for the release. Build jobs have read-only repository permissions and do not use personal keys or LoRaWAN secrets. Actions are pinned to commit SHAs; source and toolchain dependencies are pinned in `dependencies.lock.json`. The secret scanner is pinned by version and SHA256; findings are redacted.

Assets include UF2, serial-DFU ZIP, a support ZIP (USB client, protocol guide and notices), HEX, ELF, per-target build/verification manifests, and per-target SHA256 lists. Draft notes identify these native images as requiring hardware validation. The firmware version string is the selected tag. ELF files include debugging information; manifests record the exact source commit.

`.github/workflows/ci.yml` runs the same native build/test gate on branch pushes, pull requests, or manual invocation. It uploads build artifacts but does not create releases.

Inspect the [Actions runs](https://github.com/bootstrap314/thinknode_m1-infovore/actions) for the exact commit or release tag before using an artifact. Local actionlint checks establish workflow syntax, while successful GitHub runs establish that the hosted tests, downloads and builds completed. Keep preview releases marked as prereleases until the hardware acceptance scope in `HARDWARE_TESTING.md` is satisfied.
