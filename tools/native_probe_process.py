"""Launch the native interface host directly, or as a Windows binary in Wine."""
from __future__ import annotations
import os
from pathlib import Path
import sys


def add_runtime_arguments(parser):
    parser.add_argument("--dotnet", default="dotnet")
    parser.add_argument("--wine", type=Path, help="Optional Linux Wine executable for the Windows binary")
    parser.add_argument("--wine-prefix", type=Path, help="Existing disposable prefix with host root mapped at Z:")
    parser.add_argument("--windows-dotnet", type=Path, help="Windows x64 dotnet.exe used inside Wine")


def windows_host_path(path: Path) -> str:
    return "Z:\\" + str(path.resolve()).lstrip("/").replace("/", "\\")


class ProbeRuntime:
    def __init__(self, args, parser):
        self.dotnet = args.dotnet
        self.wine, self.prefix, self.windows_dotnet = args.wine, args.wine_prefix, args.windows_dotnet
        if any((self.wine, self.prefix, self.windows_dotnet)):
            if not all((self.wine, self.prefix, self.windows_dotnet)) or not sys.platform.startswith("linux"):
                parser.error("Wine requires Linux and all of --wine, --wine-prefix, --windows-dotnet")
            if not self.wine.is_file() or not self.windows_dotnet.is_file():
                parser.error("Wine and the Windows dotnet.exe must exist")
            mapped = self.prefix / "dosdevices/z:"
            if not mapped.is_symlink() or mapped.resolve() != Path("/"):
                parser.error("Initialize a disposable Wine prefix with host root mapped at Z:")
        self.windows_binary = os.name == "nt" or bool(self.wine)

    def command(self, probe: Path, *arguments: Path | str) -> list[str]:
        parameters = (probe, *arguments)
        if self.wine:
            return [str(self.wine.resolve()), windows_host_path(self.windows_dotnet),
                    *(windows_host_path(value) if isinstance(value, Path) else value for value in parameters)]
        return [self.dotnet, *(str(value.resolve()) if isinstance(value, Path) else value for value in parameters)]

    def environment(self) -> dict:
        result = os.environ.copy()
        if self.wine:
            result.update(WINEPREFIX=str(self.prefix.resolve()), WINEDEBUG="-all", DOTNET_EnableDiagnostics="0")
        return result

    def metadata(self) -> dict:
        return {"executionEnvironment": "wine" if self.wine else "native", "hostPlatform": sys.platform,
                "nativePlatform": "windows" if self.windows_binary else "linux", "actualWindowsOS": os.name == "nt",
                "unityGameTest": False}
