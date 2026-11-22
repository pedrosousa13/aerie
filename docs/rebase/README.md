# Rebase reports

One file per Helium release, `<helium-version>.md`, written by `./aerie bump`. Each lists every
patch with its apply status (clean, offset, fuzz, failed) and its `Aerie-Purpose`, and the network
traffic annotations the new Chromium added or removed. See [../helium-bump.md](../helium-bump.md).
The patch table is rewritten on every run; the traffic section can be filled in with
`./aerie traffic-diff`, and a later bump run keeps it.
