#!/bin/sh
# Run this once after cloning to configure git hooks.
chmod +x .githooks/pre-commit
git config core.hooksPath .githooks
echo "Git hooks configured. Pre-commit clang-format hook is now active."
