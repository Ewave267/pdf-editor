# Releases

See [NATIVE-RELEASES](NATIVE-RELEASES.md) for the GitHub Actions workflow,
download instructions, compatibility baseline and platform limitations.

The workflow produces Windows x64 ZIPs, Linux x86_64 portable tarballs and
AppImages, and separate Intel and Apple Silicon macOS app ZIPs. Packages include
the editor and its runtime dependencies. No release is published automatically.

Before a public release, complete clean-desktop testing and corresponding-source
distribution for the application and bundled dependencies. Preserve all license
notices. The application is GPL-3.0-only. Existing compatibility gaps in
[FORMS](FORMS.md) and [SAVING](SAVING.md) remain release gates.
