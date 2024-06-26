# Changelog

All notable changes to this project will be documented in this file. See [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) and [COMMITS.md](COMMITS.md) for commit guidelines.

## [0.3.0](https://github.com/mzdun/json-runner/compare/v0.2.4...v0.3.0) (2024-05-29)

### New Features

- support $VERSION_SHORT, set $INST to prefix ([3e4af16](https://github.com/mzdun/json-runner/commit/3e4af163e97cd799fd1dd94e6d5dde465d9f7acb))

## [0.2.4](https://github.com/mzdun/json-runner/compare/v0.2.3...v0.2.4) (2024-05-27)

### Continuous Integration

- spread ubuntu binaries (#9) ([c19eb47](https://github.com/mzdun/json-runner/commit/c19eb47eac9462d1ae5558fc36e57f9e7019ae71))

## [0.2.3](https://github.com/mzdun/json-runner/compare/v0.2.2...v0.2.3) (2024-05-27)

### Bug Fixes

- use `store` variables more evenly ([46fe353](https://github.com/mzdun/json-runner/commit/46fe353c2dd20283a281a49732cfb779bace1c9a))

## [0.2.2](https://github.com/mzdun/json-runner/compare/v0.2.1...v0.2.2) (2024-04-15)

### Bug Fixes

- git config user.name ([79355c6](https://github.com/mzdun/json-runner/commit/79355c660a8dcca545d81f1a14d5f1e341c992a3))

## [0.2.1](https://github.com/mzdun/json-runner/compare/v0.2.0...v0.2.1) (2023-09-24)

### Bug Fixes

- revert style issue ([5c80b89](https://github.com/mzdun/json-runner/commit/5c80b892718e13e37749789bb3885047a1109cee))
- properly clip at the "end" ([8fcc866](https://github.com/mzdun/json-runner/commit/8fcc866148e44bc91cb42ddfc97abc7951f01292))
- keep test dirs on demand ([9b9707d](https://github.com/mzdun/json-runner/commit/9b9707de95bcb30e969bfc2c68142c44784dd4b7))
- print env vars from Chai script ([1bc9b0c](https://github.com/mzdun/json-runner/commit/1bc9b0cad61d38f66df1b88d16027454e56bd9d4))

## [0.2.0](https://github.com/mzdun/json-runner/compare/v0.1.3...v0.2.0) (2023-09-24)

### New Features

- rename the project ([fd3eca5](https://github.com/mzdun/json-runner/commit/fd3eca5f7a7b3fb799ce12d59dd17eae8d5bed20))

### Bug Fixes

- apply small path bug fixes ([923f55e](https://github.com/mzdun/json-runner/commit/923f55e864ba15fa63ea5ae5dba48f5dfec2fe37))
- add --version, allow --help to work ([90cf92a](https://github.com/mzdun/json-runner/commit/90cf92ae137d2e39ac0394653dd2127379e8b6ca))

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
