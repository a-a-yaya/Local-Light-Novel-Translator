"""Package a prebuilt Windows Release executable and its Qt dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def run(command, **kwargs):
    return subprocess.run([str(part) for part in command], check=True, **kwargs)


def copy_module(source, target):
    target.mkdir(parents=True, exist_ok=True)
    for entry in source.iterdir():
        if entry.is_dir():
            # Each QML submodule is copied separately from the import manifest.
            if not (entry / "qmldir").exists():
                copy_module(entry, target / entry.name)
        else:
            shutil.copy2(entry, target / entry.name)


def verify_qt_source(root, qt_version, archive):
    """Require a verified source copy, distributed separately from the app ZIP."""
    metadata = root / "third_party" / "qt" / qt_version
    manifest = json.loads((metadata / "source.json").read_text(encoding="utf-8"))
    if manifest["qt_version"] != qt_version:
        raise RuntimeError("Qt source metadata does not match the selected kit")
    if not archive.is_file() or archive.name != manifest["source_filename"]:
        raise RuntimeError("Supply the Qt source archive named in source.json")
    if archive.stat().st_size != manifest["source_bytes"]:
        raise RuntimeError("Qt source archive has an unexpected size")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != manifest["sha256"]:
        raise RuntimeError("Qt source archive SHA-256 does not match source.json")
    return metadata, manifest


def qt_runtime_manifest(package, qt):
    """Record the exact Qt DLLs and reject unrecorded changes to toolkit DLLs."""
    if not package.is_dir():
        raise RuntimeError("Qt runtime package directory does not exist")
    entries = []
    compiler_dlls = {"libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"}
    for dll in sorted(package.rglob("*.dll")):
        relative = dll.relative_to(package)
        if dll.name in compiler_dlls:
            continue
        candidates = [qt / "bin" / relative, qt / relative, qt / "plugins" / relative]
        installed = next((path for path in candidates if path.is_file()), None)
        digest = hashlib.sha256(dll.read_bytes()).hexdigest()
        if installed is None or hashlib.sha256(installed.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Runtime differs from selected Qt kit: {relative}")
        entries.append({"path": relative.as_posix(), "sha256": digest,
                        "matches_installed_qt_kit": True})
    if not entries:
        raise RuntimeError("No Qt DLLs found in runtime package")
    return {"qt_version": qt.parent.name, "kit": "Windows x86_64 MinGW 11.2 shared", "files": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Prebuilt Release EXE")
    parser.add_argument("--qt", type=Path, required=True, help="Matching Qt kit directory")
    parser.add_argument("--mingw", type=Path, required=True, help="Matching MinGW directory")
    parser.add_argument("--qt-source", type=Path, required=True,
                        help="Verified corresponding Qt source archive (published separately)")
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    qt, mingw = args.qt.resolve(), args.mingw.resolve()
    binary, output = args.binary.resolve(), args.output.resolve()
    if not binary.is_file():
        parser.error("Release EXE does not exist")
    qt_version = qt.parent.name
    source_metadata, source_manifest = verify_qt_source(root, qt_version, args.qt_source.resolve())
    version = re.search(r"project\(kakuCH01\s+VERSION\s+([\d.]+)",
                        (root / "kakuCH01/CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    stem = f"LightNovelTranslator-{version}-Windows-x64"
    package, archive = output / stem, output / (stem + ".zip")
    if package.exists() or archive.exists():
        parser.error("Output already exists; choose a fresh --output directory")
    os.environ["PATH"] = str(mingw / "bin") + os.pathsep + str(qt / "bin") + os.pathsep + os.environ["PATH"]
    output.mkdir(parents=True, exist_ok=True)
    package.mkdir()
    executable = package / "LightNovelTranslator.exe"
    shutil.copy2(binary, executable)
    shutil.copy2(root / "kakuCH01/icons/app.ico", package / "LightNovelTranslator.ico")
    imports = json.loads(run([qt / "bin/qmlimportscanner.exe", "-rootPath", root / "kakuCH01/qml",
                              "-importPath", qt / "qml"], capture_output=True, text=True,
                             encoding="utf-8").stdout)
    qml_base = (qt / "qml").resolve()
    modules = {Path(item["path"]).resolve() for item in imports
               if item.get("type") == "module" and item.get("path") and Path(item["path"]).is_dir()}
    for module in sorted(modules):
        if module.is_relative_to(qml_base):
            copy_module(module, package / "qml" / module.relative_to(qml_base))
    plugins = sorted((package / "qml").rglob("*.dll"))
    deploy = [qt / "bin/windeployqt.exe", "--release", "--no-quick-import", "--compiler-runtime",
              "--no-opengl-sw", "--no-system-d3d-compiler", "--skip-plugin-types",
              "generic,platforminputcontexts,qmltooling", "--translations", "zh_CN", "--dir",
              package, executable, *plugins]
    log_path = output / (stem + "-deploy.log")
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run([str(part) for part in deploy], stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        # Some Qt 6.5 MinGW deploy tools misclassify PE plugin binaries. Only
        # allow the alternate selection after checking the actual plugin metadata.
        metadata = run([qt / "bin/qtplugininfo.exe", qt / "plugins/platforms/qwindows.dll"],
                       capture_output=True, text=True, encoding="utf-8")
        message = metadata.stdout + metadata.stderr
        if "Unable to find the platform plugin" not in log_path.read_text(encoding="utf-8") or "(release)" not in message:
            raise RuntimeError(f"Qt deployment failed: {log_path}")
        deploy[1] = "--debug"
        with log_path.open("a", encoding="utf-8") as log:
            log.write("\nVerified installed platform plugin metadata is release; retrying deploy selection.\n")
            run(deploy, stdout=log, stderr=subprocess.STDOUT)
    (package / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\nQml2Imports=qml\n", encoding="utf-8")
    image_formats = package / "imageformats"
    image_formats.mkdir(exist_ok=True)
    shutil.copy2(qt / "plugins/imageformats/qico.dll", image_formats / "qico.dll")
    shutil.copy2(root / "README.md", package / "README.md")
    shutil.copy2(root / "kakuCH01/docs/USER_GUIDE.md", package / "USER_GUIDE.md")
    guide_directory = package / "kakuCH01/docs"
    guide_directory.mkdir(parents=True)
    shutil.copy2(root / "kakuCH01/docs/USER_GUIDE.md", guide_directory / "USER_GUIDE.md")
    shutil.copy2(root / "免责声明.md", package / "免责声明.md")
    (package / "使用前请读.txt").write_text(
        f"Light Novel Translator {version}\n\n"
        "完整解压后运行 LightNovelTranslator.exe，不要在压缩包内运行。\n"
        "先打开「设置 → 使用说明」，按说明下载推理后端和 GGUF。\n"
        "本程序使用 LGPLv3 许可的 Qt；许可、对应源码及替换说明见 QT_LGPL_COMPLIANCE.md。\n"
        "问题反馈：https://github.com/a-a-yaya/Local-Light-Novel-Translator/issues\n", encoding="utf-8-sig")
    licenses = package / "licenses"
    licenses.mkdir()
    sdk = qt.parents[1]
    for name in ["LICENSE", "Copyright.txt", "LICENSE.FDL"]:
        source = sdk / "Licenses" / name
        if not source.is_file():
            raise RuntimeError(f"Missing Qt license document: {source}")
        shutil.copy2(source, licenses / ("Qt-" + name.replace(".txt", "") + ".txt"))
    for name in ["gcc", "winpthreads"]:
        shutil.copytree(mingw / "licenses" / name, licenses / name)
    documentation = sdk / "Docs" / ("Qt-" + qt_version)
    for name in ["qtcore", "qtgui", "qtnetwork", "qtqml", "qtquick", "qtquickcontrols",
                 "qtquickdialogs", "qtquicklayouts", "qtquickeffects", "qtsvg", "qtshadertools", "qtcore5compat"]:
        entries = list((documentation / name).glob("*attribution*.html"))
        if entries:
            target = licenses / "qt-attributions" / name
            target.mkdir(parents=True)
            for entry in entries:
                shutil.copy2(entry, target / entry.name)
    (package / "THIRD_PARTY_NOTICES.md").write_text(
        f"# Third-party components\n\nThis application dynamically links Qt {qt_version} from The Qt Company and other contributors. "
        "It also includes MinGW GCC runtime libraries and winpthreads. Dependency license texts and Qt attribution documents "
        "are included in `licenses/`; those texts do not assign a license to the application source.\n\n"
        f"Qt libraries are used under LGPLv3. Full GPLv3 and LGPLv3 texts are in licenses/Qt-LICENSE.txt.\n"
        f"Corresponding Qt source (maintainer-hosted copy): {source_manifest['distribution_url']}\n"
        "Source checksums, toolkit details and replacement instructions: QT_LGPL_COMPLIANCE.md and third_party/qt/.\n"
        "GCC source: https://gcc.gnu.org/releases.html\nMinGW-w64 source: https://www.mingw-w64.org/\n"
        "Application source: https://github.com/a-a-yaya/Local-Light-Novel-Translator\n\n"
        "Qt shared libraries may be replaced with interface-compatible versions. Reverse engineering for debugging "
        "modifications to those libraries is not restricted by this notice. See the included license texts.\n", encoding="utf-8")
    shutil.copy2(root / "QT_LGPL_COMPLIANCE.md", package / "QT_LGPL_COMPLIANCE.md")
    packaged_metadata = package / "third_party" / "qt" / qt_version
    shutil.copytree(source_metadata, packaged_metadata)
    (packaged_metadata / "runtime-sha256.json").write_text(
        json.dumps(qt_runtime_manifest(package, qt), indent=2) + "\n", encoding="utf-8")
    source_copy = output / source_manifest["source_filename"]
    if source_copy.exists() and source_copy.resolve() != args.qt_source.resolve():
        with source_copy.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != source_manifest["sha256"]:
                raise RuntimeError("Existing output Qt source archive has a different checksum")
    elif source_copy.resolve() != args.qt_source.resolve():
        shutil.copy2(args.qt_source, source_copy)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for entry in sorted(package.rglob("*")):
            if entry.is_file():
                bundle.write(entry, entry.relative_to(output).as_posix())
    with archive.open("rb") as stream:
        checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    (output / "SHA256SUMS.txt").write_text(
        checksum + "  " + archive.name + "\n"
        + source_manifest["sha256"] + "  " + source_manifest["source_filename"] + "\n", encoding="utf-8")
    print(json.dumps({"archive": str(archive), "bytes": archive.stat().st_size, "sha256": checksum}, indent=2))


if __name__ == "__main__":
    main()
