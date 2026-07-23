# Agent notes

- Specs in `docs/` are the contract; do not invent pinouts or Modbus maps.
- Prefer libraries in `docs/LIBRARIES.md`.
- Cross-check RAK9154 against `forest-weather-machines/rak-4-5-wire` (local sibling).
- Never commit secrets, `*.env`, keys, or live OTAA AppKeys.
- No aspirational “deployed” claims without bench/TTN evidence.
- Null sensor readings stay null — never fabricate zeros.
