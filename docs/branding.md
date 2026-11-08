# Branding notes

## Linux app ID

The Linux app ID is `me.pedrosousa.Aerie`. It names the metainfo file
(`packaging/linux/me.pedrosousa.Aerie.metainfo.xml`) and is the RDN in
`patches/branding/0003-linux-desktop-identity.patch`.

The domain `pedrosousa.me` is temporary. The ID will change once the final
domain is chosen. When it changes, update the patch, the metainfo file name
and content, `packaging/linux/install.sh` and `tests/test_packaging.py`
together.
