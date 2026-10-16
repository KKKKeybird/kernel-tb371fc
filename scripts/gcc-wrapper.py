#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Transparent compiler wrapper (python3).
#
# The upstream 4.19 script is python2-only and breaks when invoked by python3.
# The TB371FC build line always passes CC=clang explicitly, which overrides the
# Makefile's wrapper assignment, so this script is a passthrough kept only so
# that any accidental wrapper invocation still works.
import os
import subprocess
import sys

if len(sys.argv) < 2:
    sys.exit("usage: gcc-wrapper.py <compiler> [args...]")

sys.exit(subprocess.call(sys.argv[1:]))
