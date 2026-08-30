# Config

Optional, non-secret application defaults that vary by deployment rather than
by PCB. Fixed hardware facts belong in `boards/`; per-application build
settings belong in `profiles/`.

The selected application reads these values and passes them explicitly to
component configuration structures. Components never search global
configuration themselves (DESIGN_DOC.md section 13).

**Secrets — WiFi credentials in particular — must not be committed here.**
`config/local.cmake` is gitignored and is the intended place for local,
uncommitted settings.

Nothing is defined yet; the `minimal` application needs no configuration.
