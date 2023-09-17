# Changelog

All notable changes to this project will be documented in this file. See [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) and [COMMITS.md](COMMITS.md) for commit guidelines.

## [0.1.3](https://github.com/mzdun/runner/compare/v0.1.2...v0.1.3) (2023-09-17)

### Bug Fixes

- config git (builtin) (#6) ([31a3c7b](https://github.com/mzdun/runner/commit/31a3c7b1db1d935ece312af40dd6634c71178d11))

## [0.1.2](https://github.com/mzdun/runner/compare/v0.1.1...v0.1.2) (2023-08-14)

### Bug Fixes

- install current JSON schema alongside runner (#4) ([d6b136c](https://github.com/mzdun/runner/commit/d6b136c119ed766577b85f43eda793f1710554ac))

## [0.1.1](https://github.com/mzdun/runner/compare/v0.1.0...v0.1.1) (2023-08-14)

### Bug Fixes

- lang is string (#3) ([b0bcb4f](https://github.com/mzdun/runner/commit/b0bcb4ff5d710bfe444f15c0415438860762d608))

## [0.1.0](https://github.com/mzdun/runner/compare/v0.0.0...v0.1.0) (2023-08-14)

### New Features

- attach piping options to JSON test ([16900a5](https://github.com/mzdun/runner/commit/16900a50ca0ad20123ee2d567ac9cd58ed4ac266))
- allow "|" and "> /dev/null" on new run ([717fc04](https://github.com/mzdun/runner/commit/717fc04da1c2e1b5e86e07a71fe99abc1f09800f))
- keep debug info with mt reports ([234a940](https://github.com/mzdun/runner/commit/234a940160c5875acbfedb91b865172d13d48f1d))
- hide prepare until failure ([12bae84](https://github.com/mzdun/runner/commit/12bae842df421e142c00ee1dff248c8f62a4fd9e))
- posix run + compile with gcc ([b50e946](https://github.com/mzdun/runner/commit/b50e9463af3bebeddaff9607349f4e24249c76a5))

### Bug Fixes

- use arch with 32KiB array instead of a 10MiB vector (#2) ([008e9ae](https://github.com/mzdun/runner/commit/008e9ae3b62feeaa82399450cd4dd4a115f81093))
- allow io::run to be multithreaded on posix ([819be21](https://github.com/mzdun/runner/commit/819be21f4f7eb68e1b30ee25eeac22c232cded46))
