# SES — sensor firmware

Each sensor package has its own folder and version. This repository retains the original Signal Finder file and history.

| Package | Current version | Start here |
| --- | --- | --- |
| Turbidity + depth / BoSL 0.5.x / SIM7000G | 1.0.1 | [Setup](sensor-packages/turbidity-depth/README.md) · [Steps and findings](sensor-packages/turbidity-depth/WORKFLOW_SUMMARY.md) |
| Signal Finder | Historical/unversioned | [Original sketch](Signal_Checking_V1.ino) |

The turbidity-depth source is available on main and the package development branch `codex/turbidity-depth`. Raw customer logs and internal discovery archives are excluded from this public repository.

## Version management

Keep new packages in `sensor-packages/<package-name>/`, each with README, VERSION, RELEASE.json and CHANGELOG.md. Do not mix different sensor wiring/packages into one sketch folder.

Use package-specific development branches and independent semantic versions: patch for compatible fixes, minor for compatible features, major for breaking hardware/configuration/data interfaces. Record edit timestamp with timezone, exact changes, dependencies and real verification results. A Git commit identifies an exact snapshot; branch names are not immutable releases. Future release tags should use `<package>-vX.Y.Z` and never be moved. No Git tag is created by this documentation publication.

Before public commits, remove credentials, SIM identifiers and raw field logs. Keep deployment-specific site/APN settings separately. Review the package's validation limits rather than interpreting a version number as universal network compatibility.
