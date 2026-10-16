"""PlatformIO sorts the linker flags and so breaks the --start-group/--end-group pair that the
esp_new_jpeg component adds around its library. The library is one archive and needs no group,
so remove the stray flags from the link line (the group PlatformIO builds around the libraries stays)."""
Import("env")  # noqa: F821

STRAY = ("-Wl,--start-group", "-Wl,--end-group")


def strip(node, **kw):
    env.Replace(LINKFLAGS=[f for f in env["LINKFLAGS"] if f not in STRAY])  # noqa: F821


strip(None)
