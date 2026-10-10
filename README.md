# PDF Editor

An open-source desktop app for filling PDF forms and adding text, images,
signatures and annotations. Your documents stay on your computer.

## Download and run

**[Download the development prerelease](https://github.com/Ewave267/pdf-editor/releases)**

Choose the newest **Development preview** and expand **Assets**. These are
unstable test builds, not stable releases. No compilation or separate Qt
installation is needed.

| System | Download | Run |
| --- | --- | --- |
| Windows x64 | `pdf-editor-windows-x64.zip` | Extract fully, then open `pdf-editor.exe`. Keep the DLLs and folders together. |
| Linux x86_64 or ARM64 | Matching `.AppImage` or `.tar.gz` | Make the AppImage executable and run it, or extract the tarball and run `./pdf-editor`. |
| macOS Intel or Apple Silicon | Matching `macos-x64.zip` or `macos-arm64.zip` | Extract and open `PDF Editor.app`. |

Linux packages target RHEL 9, Fedora and Ubuntu 22.04 or newer. Windows builds
are unsigned and macOS builds are not notarized, so security warnings may appear.
See the [download and platform guide](docs/NATIVE-RELEASES.md) for details.

## What you can do

- Fill supported PDF forms, including AcroForm and XFA.
- Add text with font, size, bold, italic and underline controls.
- Add images, drawn or image-based signatures, highlights and shapes.
- Organize pages, search, print, undo changes and save an edited copy.

Start with **Open**, make your changes, then choose **Save a copy**.
See the [desktop guide](docs/DESKTOP-EXPERIENCE.md) for a walkthrough.

Existing PDF text cannot yet be edited directly. Signatures are visual additions,
not cryptographic signatures. Added content cannot currently be saved in dynamic
XFA documents. See [form compatibility](docs/FORMS.md) and
[saving limits](docs/SAVING.md).

## Development

Built with C++20, Qt Quick/QML and PDFium. PDF rendering runs in an isolated worker.

- [Build and package the desktop app](docs/NATIVE-RELEASES.md)
- [Viewer setup and tests](docs/VIEWER.md)
- [Roadmap](docs/ROADMAP.md) and [progress](PROGRESS.md)

## License

[GNU GPL version 3 only](LICENSE) (`GPL-3.0-only`).
