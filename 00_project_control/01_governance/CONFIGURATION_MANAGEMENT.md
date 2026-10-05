# Configuration Management

Track PCB, firmware, AI, parameters, calibration, test environment, and release artifacts as one reproducible configuration.

## Versioning
Use semantic versions for software and explicit revision identifiers for hardware. Never overwrite released artifacts.

## Required Release Manifest
- git commit
- PCB revision
- BOM revision
- firmware build hash
- parameter checksum
- AI model checksum
- companion image/package version
- test report references
