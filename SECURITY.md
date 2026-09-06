<!--
SPDX-FileCopyrightText: 2026 Rupam Mondal
SPDX-License-Identifier: MIT
-->

# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| Latest release | Yes |
| `main` development branch | Best effort |
| Older releases | No |

## Reporting a vulnerability

Do not report security vulnerabilities through a public GitHub issue.

Use GitHub’s private vulnerability-reporting feature:

1. Open the repository’s **Security** tab.
2. Select **Advisories**.
3. Select **Report a vulnerability**.
4. Describe the issue and its possible impact.
5. Include reproduction steps when safe.
6. Suggest a fix if available.

## Include in the report

- Affected version or commit.
- Affected hardware configuration.
- Vulnerability description.
- Reproduction steps.
- Expected and actual behavior.
- Possible impact.
- Relevant logs with credentials removed.
- Suggested mitigation, if known.

## Sensitive information

Never include:

- Wi-Fi passwords.
- API keys.
- Access tokens.
- Private keys.
- Personal information.
- Cloud credentials.
- Unredacted device identifiers.

## Security scope

Relevant security areas include:

- BLE telemetry integrity.
- BLE connection handling.
- Packet validation.
- CRC validation.
- Serial command handling.
- NVS configuration handling.
- Integer encoding and bounds.
- Sensor-data validation.
- Dependency vulnerabilities.
- Build and release integrity.

## Current limitations

- BLE telemetry does not use application-level encryption.
- The firmware does not configure an application-level BLE passkey.
- CRC-16 detects transmission corruption but is not cryptographic authentication.
- Physical access to the ESP32-C3 and Serial interface is outside the current threat model.
- HydroSense is not certified as a safety-critical or industrial control system.

## Response process

1. The report will be reviewed privately.
2. The affected code and versions will be identified.
3. A correction will be developed and tested.
4. A security advisory may be published after a fix is available.
5. The fix will be documented in `CHANGELOG.md`.
6. A corrected release will be created when necessary.

## Responsible disclosure

Please allow reasonable time to investigate and correct a vulnerability before public disclosure.

Good-faith security research that respects privacy, avoids data destruction and follows this policy is welcomed.