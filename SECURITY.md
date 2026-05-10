# Security Policy

## Reporting

If you discover a security issue in this repository, do not publish exploit details in a public issue first.

Report the problem privately to the project owner or maintainer with:

- a clear description of the issue
- the affected files or commands
- impact
- reproduction steps
- any suggested mitigation

## In Scope

Security-relevant areas include:

- unsafe file handling
- path traversal in CLI tools
- unsafe overwrite or truncation behavior
- malformed input handling
- resume or progress corruption leading to silent data misuse
- dependency or runtime loading issues

## Out Of Scope

This repository is a local tooling project. It does not currently expose a public hosted service, authentication boundary, or remote API surface.

## Handling Guidance

- preserve failing artifacts when possible
- record the exact command and inputs used
- prefer minimal isolated repro steps
- avoid destructive cleanup before evidence is captured
