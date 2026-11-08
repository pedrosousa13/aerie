# Bumping Helium

Aerie follows Helium's releases to stay on Chromium's security releases. One
command moves the pins, and a daily job opens a pull request when a new release
exists.

## The command

```sh
./aerie bump --check   # only look; exit 20 if a newer release exists
./aerie bump           # move the pins, then run the patch check
```

`./aerie bump` does these things, in this order:

1. Reads the tag lists of the two pinned upstream repositories
   (`github.com/imputnet/helium` and `github.com/imputnet/helium-linux`) with
   `git ls-remote`, and takes the newest `A.B.C.D` tag of `helium-linux`. The
   matching `helium` tag is `A.B.C`. `--to TAG` picks another release tag.
2. Checks that `.gitmodules` points at those two URLs, and that the files
   `helium.lock` and the two submodules have no uncommitted changes.
3. Fetches each tag from its upstream URL into the submodule. The submodule
   moves only if the fetched tag is the commit that `ls-remote` listed, and if
   the two checkouts build the Helium version of the tag. If a step fails, the
   submodules go back to where they were and `helium.lock` is not written.
4. Rewrites `helium.lock`: the versions, both commits, the sha256 of each
   download (Chromium tarballs from the `SHA256SUMS` of the
   `imputnet/chromium-tarballs` release, the others from Helium's `deps.ini`)
   and the official Chromium tarball in `[fallback.chromium]` (from the
   `.hashes` file next to it).
5. Prints the old and new versions:

   ```
   Helium 0.21.1.1 -> 0.22.1.1
   Chromium 157.0.8103.38 -> 158.0.1.2
   ```

6. Downloads the new tarballs into the build root if they are not there (check
   them against the new lock), then runs `./aerie patches check` on the new pins.
   Every patch that no longer applies is listed by name with the hunk that
   failed, and the command exits with status 3. `--no-check` skips this step.
   `--report FILE` also writes the result as Markdown.

The command commits nothing. It does not build, and it does not edit the
[dependency ledger](helium-dependency-ledger.md), the dropped-patch lists or the
vendored crate lock. Those are the fix-up routine below.

| Exit status | Meaning |
| --- | --- |
| 0 | Up to date, or bumped and every patch applies |
| 1 | An error. Before the pins move (tag lookup, `.gitmodules`, a dirty tree, a tag that does not match) nothing changes, and a failure while the pins move puts them back. An error in the later download or patch-check step leaves the new pins in place |
| 3 | Bumped, but a patch no longer applies. The pins are changed |
| 20 | `--check` only: a newer release exists |

## The fix-up routine

After `./aerie bump` exits with 3, or when the bump PR is a draft:

1. Read the failing patches. A line `FAILED  aerie/<patch>` is followed by the
   output of `patch` and by the hunk that did not apply (`failed hunk in <file>`).
   A patch that depends on a failed one is `SKIPPED`.
2. Refresh the patch against the new text. For an Aerie patch, follow the rules
   in [CONTRIBUTING.md](../CONTRIBUTING.md). For a Helium patch that Aerie drops or
   adapts, see "Dropping or adapting a Helium patch" there.
3. Run `./aerie patches check` until it passes.
4. Open [the ledger](helium-dependency-ledger.md): a row that names a Helium
   patch that moved or went away needs a fix. The tests fail when a row names a
   patch that is not in the pinned series.
5. After a Chromium change, `./aerie crates resolve` can have new work to do
   (see [adblock.md](adblock.md)), and so can the filter lists
   (`./aerie lists update`).
6. Run the tests, then `./aerie build --clean` and the Bench, or let the gate do
   it (below). Then `./aerie prune`.

## The scheduled job

`.github/workflows/bump-helium.yml` runs every day at 03:47 UTC (after the
mirror sync) and on demand. It runs `./aerie bump --check`:

- No newer release: the job ends. It opens nothing.
- A newer release, and a branch `bump-helium-<tag>` already has a pull request
  (open or closed): the job ends. A closed pull request is not opened again.
- Otherwise it runs `./aerie bump --to <tag>` on the runner, commits
  `helium.lock` and the submodule pins on the branch `bump-helium-<tag>`, pushes
  it and opens a pull request with the report as its body. If the patch check
  failed, the pull request is a draft.

The job reads releases only from the two upstream repositories above. The tag
name goes to the shell only through an environment variable, after the check
that it has the form `A.B.C.D`.

The pull request still has to pass the gate. The gate (see [ci.md](ci.md)) runs
the build and the Bench on pull requests authored by the maintainer, so the job
opens the pull request with the maintainer's token. The job cannot merge.

### One-time setup (maintainer only)

The job needs the repository secret `BUMP_PR_TOKEN`. Without it the job fails
with a message that says so, but only on a day with a new release.

The secret is set. It holds the maintainer's `gh` login token (a classic OAuth
token with the scopes `repo`, `read:org`, `gist` and `admin:public_key`; no
`workflow` scope, which the bump does not need). It was stored with:

```sh
gh auth token | gh secret set BUMP_PR_TOKEN -R pedrosousa13/aerie
```

Bump pull requests are therefore authored by the maintainer, which is what the
gate needs. The job uses the token only in the two steps that talk to GitHub
(the pull request lookup, and the push and `gh pr create`). It does not give it
to the step that runs `./aerie bump`.

The secret stops working when the maintainer runs `gh auth logout` or revokes
the token (GitHub, Settings, Developer settings,
Personal access tokens). The job then fails on the next day with a new release. Store the
current token again with the command above.

After this is merged, run the job once by hand to see that it passes:
`gh workflow run bump-helium.yml -R pedrosousa13/aerie`. With no new release it
prints the versions and ends.

#### Alternative: a fine-grained token

A fine-grained token is narrower and does not depend on the `gh` login:

1. On GitHub open Settings, Developer settings, Personal access tokens,
   Fine-grained tokens, Generate new token.
2. Resource owner: `pedrosousa13`. Repository access: only `aerie`.
3. Repository permissions: Contents: Read and write. Pull requests: Read and
   write. Nothing else. (A token with Workflows permission is not needed: the
   bump never changes a workflow file.)
4. Store it, and paste the token when `gh` asks:
   ```sh
   gh secret set BUMP_PR_TOKEN -R pedrosousa13/aerie
   ```

The schedule runs only from the default branch.
