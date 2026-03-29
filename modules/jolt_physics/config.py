def can_build(env, platform):
    if env.msvc:
        import methods

        # Visual Studio 2017 cannot reliably build
        # Jolt.
        if methods.get_compiler_version(env)["major"] < 16:
           return False
        
    return (
        not env["disable_3d"] and
        not env["arch"] == "ppc32"
    )


def configure(env):
    pass
