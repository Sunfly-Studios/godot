import os
import sys

def can_build():
    return sys.platform.startswith("haiku")

def get_opts():
    return []

def get_doc_classes():
    return ["EditorExportPlatformHaiku"]

def get_doc_path():
    return "doc_classes"

def get_flags():
    return []

def configure(env):
    env.Append(CPPPATH=['#platform/haiku'])
    if env["target"] == "editor":
        env.Append(CCFLAGS=['-O2'])