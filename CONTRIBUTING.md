# Contributing

Thanks for improving this project.

## Ways to help

- Bug reports with hardware details and Serial logs
- Documentation fixes and translations
- Hardware variants (different relays, displays, sensors)
- Features that keep the single-file Arduino sketch usable

## Development tips

1. Fork and branch from `main`.
2. Keep pin defaults stable; document any changes in `docs/WIRING.md`.
3. Do not commit Wi-Fi passwords or personal IPs.
4. Test Mode A, Mode B, flow cal, distance cal, lock, and reset on real hardware when possible.
5. Match existing code style (2-space indent, clear state names, Serial traces on major transitions).

## Pull request checklist

- [ ] Builds on Arduino-ESP32 3.x (or documented core version)
- [ ] No secrets in the diff
- [ ] README / docs updated if behavior or pins change
- [ ] Describe test steps performed

## Code of conduct

Be respectful. Assume good intent. No harassment or gatekeeping.
