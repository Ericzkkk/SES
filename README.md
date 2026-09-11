# SES — sensor firmware

Each sensor package has its own folder and version. This repository retains the original Signal Finder file and history.

| Package | Current version | Start here |
| --- | --- | --- |
| Turbidity + depth / BoSL 0.5.x / SIM7000G | 1.0.1 | [Setup](sensor-packages/turbidity-depth/README.md) · [Steps and findings](sensor-packages/turbidity-depth/WORKFLOW_SUMMARY.md) |
| Network Discovery (LTE-M/NB-IoT diagnostics) | SD firmware v4; docs 1.0.2 | [Independent workflow and code](network-discovery/README.md) |
| Supervisor's Signal Finding | Earlier separate project; unchanged | [Original sketch](Signal_Checking_V1.ino) |

The turbidity-depth source is available on main and the package development branch `codex/turbidity-depth`. Raw customer logs and internal discovery archives are excluded from this public repository.

## Version management

Keep new packages in `sensor-packages/<package-name>/`, each with README, VERSION, RELEASE.json and CHANGELOG.md. Do not mix different sensor wiring/packages into one sketch folder.

Use package-specific development branches and independent semantic versions: patch for compatible fixes, minor for compatible features, major for breaking hardware/configuration/data interfaces. Record edit timestamp with timezone, exact changes, dependencies and real verification results. A Git commit identifies an exact snapshot; branch names are not immutable releases. Future release tags should use `<package>-vX.Y.Z` and never be moved. No Git tag is created by this documentation publication.

Before public commits, remove credentials, SIM identifiers and raw field logs. Keep deployment-specific site/APN settings separately. Review the package's validation limits rather than interpreting a version number as universal network compatibility.

## Keep projects separate

`network-discovery/` is the current LTE-M/NB-IoT diagnostic workflow. `sensor-packages/turbidity-depth/` contains only the production sensor package and its own history. The supervisor's earlier Signal Finding code is separate and has not been moved or edited. Private customer logs remain in network-finder.
