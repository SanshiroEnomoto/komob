#!/bin/sh
set -eu

if [ -d .git-hooks ]; then
  git config core.hooksPath .git-hooks
  echo "Configured core.hooksPath = .git-hooks"
else
  echo "Run this command at the repository root directory"
fi
