def can_build(env, platform):
    return (
        env["sse2"] and
        not env["disable_3d"] and
        not env["arch"] == "ppc32"
    )


def configure(env):
    pass
