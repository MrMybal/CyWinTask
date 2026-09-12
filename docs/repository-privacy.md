# Repository privacy

Use the GitHub noreply email address from your account settings for commits. The
privacy check also checks annotated tag identities. Keep personal paths, private
environment files, certificates, local assistant state and runtime data outside Git.

Enable the local pre-commit check in a fresh clone:

```sh
git config user.email "9800352+MrMybal@users.noreply.github.com"
git config core.hooksPath .githooks
```

Python 3 is required for the local check. GitHub Actions additionally scans the
reachable commit history with Gitleaks. These checks reduce accidental exposure;
they are not a substitute for reviewing staged files and release contents.

Review `git diff --cached` before committing. Use relative project paths and
synthetic screenshots. Never put credentials into reports or issue attachments.
Build outputs and release archives need a separate privacy review before uploading.
Changing Git history does not remove old installers, remote caches or third-party
clones. After an authorized history rewrite, use a fresh clone rather than merging
or pushing an old checkout, which can reintroduce removed data.

## Project publishing policy

Use `9800352+MrMybal@users.noreply.github.com` for both author and committer
identities. Never publish real machine diagnostics, process lists, hardware
inventories, user profile paths, logs, dumps or personal screenshots. Use synthetic
data for tests and demonstrations intended for sharing. Hardware integration
checks remain local; do not publish their output or use it as demonstration data.

After a privacy history rewrite, prepare publication from the cleaned remote
history in a fresh clone and preserve local edits separately. Never merge an old
branch or push local tool checkpoint refs. Keep the privacy hook and workflow
enabled. Review tracked files, the staged diff and the actual contents of any
future distribution before uploading it. Local installers and ZIP files are not
approved for publication merely because source checks pass.