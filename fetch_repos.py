#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT

"""Fetch fixed component revisions and apply project-specific adaptations."""

from __future__ import annotations

import argparse
import json
import os
import stat
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CONFIG = SCRIPT_DIR / "repos.json"


class FetchError(RuntimeError):
    pass


@dataclass(frozen=True)
class Repository:
    name: str
    url: str
    path: Path
    ref: str
    commit: str
    patches: tuple[Path, ...]
    overlays: tuple[tuple[Path, Path], ...]
    remove: tuple[Path, ...]


def run(command: list[str], *, cwd: Path | None = None, capture: bool = False) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if result.returncode != 0:
        details = (result.stderr or result.stdout or "").strip()
        suffix = f"\n{details}" if details else ""
        raise FetchError(f"Command failed ({result.returncode}): {' '.join(command)}{suffix}")
    return (result.stdout or "").strip()


def command_succeeds(command: list[str], *, cwd: Path) -> bool:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def remove_tree(path: Path) -> None:
    def make_writable_and_retry(function: Any, target: str, _: Any) -> None:
        os.chmod(target, stat.S_IWRITE)
        function(target)

    shutil.rmtree(path, onerror=make_writable_and_retry)


def back_up_tree(source: Path, backup: Path) -> None:
    """Copy a tree to backup before removing its possibly read-only source."""
    shutil.copytree(source, backup)
    try:
        remove_tree(source)
    except Exception:
        if source.exists():
            remove_tree(source)
        shutil.copytree(backup, source)
        raise


def confined_path(root: Path, value: str, field: str) -> Path:
    path = (root / value).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise FetchError(f"{field} escapes its root: {value}") from error
    return path


def load_repositories(config_path: Path) -> list[Repository]:
    try:
        raw: Any = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FetchError(f"Cannot read {config_path}: {error}") from error

    if not isinstance(raw, list) or not raw:
        raise FetchError("Repository config must be a non-empty JSON array")

    repositories: list[Repository] = []
    names: set[str] = set()
    paths: set[Path] = set()
    for index, item in enumerate(raw):
        if not isinstance(item, dict):
            raise FetchError(f"Repository entry {index} must be an object")
        try:
            name = str(item["name"])
            url = str(item["url"])
            relative_path = Path(str(item["path"]))
            ref = str(item["ref"])
            commit = str(item["commit"])
        except KeyError as error:
            raise FetchError(f"Repository entry {index} is missing {error.args[0]}") from error

        if name in names:
            raise FetchError(f"Duplicate repository name: {name}")
        if relative_path.is_absolute() or ".." in relative_path.parts:
            raise FetchError(f"Unsafe repository path for {name}: {relative_path}")
        if relative_path in paths:
            raise FetchError(f"Duplicate repository path: {relative_path}")
        if len(commit) != 40 or any(char not in "0123456789abcdefABCDEF" for char in commit):
            raise FetchError(f"Commit for {name} must be a full 40-character SHA")

        patch_paths = tuple(
            confined_path(SCRIPT_DIR, str(value), f"patch for {name}")
            for value in item.get("patches", [])
        )
        overlays: list[tuple[Path, Path]] = []
        for overlay in item.get("overlays", []):
            if not isinstance(overlay, dict) or "source" not in overlay or "destination" not in overlay:
                raise FetchError(f"Invalid overlay for {name}")
            source = confined_path(SCRIPT_DIR, str(overlay["source"]), f"overlay source for {name}")
            destination = Path(str(overlay["destination"]))
            if destination.is_absolute() or ".." in destination.parts:
                raise FetchError(f"Unsafe overlay destination for {name}: {destination}")
            overlays.append((source, destination))

        remove = tuple(Path(str(value)) for value in item.get("remove", []))
        if any(path.is_absolute() or ".." in path.parts for path in remove):
            raise FetchError(f"Unsafe remove path for {name}")

        names.add(name)
        paths.add(relative_path)
        repositories.append(
            Repository(
                name=name,
                url=url,
                path=relative_path,
                ref=ref,
                commit=commit.lower(),
                patches=patch_paths,
                overlays=tuple(overlays),
                remove=remove,
            )
        )
    return repositories


def validate_assets(repositories: list[Repository]) -> None:
    for repository in repositories:
        for patch in repository.patches:
            if not patch.is_file():
                raise FetchError(f"Patch does not exist for {repository.name}: {patch}")
        for source, _ in repository.overlays:
            if not source.exists():
                raise FetchError(f"Overlay does not exist for {repository.name}: {source}")


def select_repositories(repositories: list[Repository], selected: list[str]) -> list[Repository]:
    if not selected:
        return repositories
    by_name = {repository.name: repository for repository in repositories}
    unknown = sorted(set(selected) - by_name.keys())
    if unknown:
        raise FetchError(f"Unknown repositories: {', '.join(unknown)}")
    selected_names = set(selected)
    return [repository for repository in repositories if repository.name in selected_names]


def destination_for(output_root: Path, repository: Repository) -> Path:
    return confined_path(output_root, repository.path.as_posix(), f"destination for {repository.name}")


def adaptation_state(repository: Repository, destination: Path) -> str:
    if not repository.patches and not repository.remove and not repository.overlays:
        return "no adaptations"

    patches_applied = all(
        command_succeeds(["git", "apply", "--reverse", "--check", str(patch)], cwd=destination)
        for patch in repository.patches
    )
    removals_applied = all(not (destination / relative_path).exists() for relative_path in repository.remove)
    if patches_applied and removals_applied:
        return "adapted"

    patches_pending = all(
        command_succeeds(["git", "apply", "--check", str(patch)], cwd=destination)
        for patch in repository.patches
    )
    removals_pending = all((destination / relative_path).exists() for relative_path in repository.remove)
    if patches_pending and removals_pending:
        return "upstream baseline"
    return "adaptations diverged"


def check_repositories(repositories: list[Repository], output_root: Path) -> None:
    validate_assets(repositories)
    for repository in repositories:
        destination = destination_for(output_root, repository)
        if not destination.exists():
            print(f"MISSING  {repository.name:<24} {repository.path}")
            continue

        git_marker = destination / ".git"
        if not git_marker.exists():
            adaptation = adaptation_state(repository, destination)
            print(f"VENDORED {repository.name:<24} {adaptation}")
            continue

        try:
            head = run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True)
            status = run(["git", "status", "--short"], cwd=destination, capture=True)
        except FetchError as error:
            print(f"INVALID  {repository.name:<24} {error}")
            continue
        revision = "MATCH" if head.lower() == repository.commit else "MISMATCH"
        state = "modified" if status else "clean"
        adaptation = adaptation_state(repository, destination)
        print(f"{revision:<8} {repository.name:<24} {head[:12]} {state}, {adaptation}")


def apply_overlay(source: Path, destination: Path) -> None:
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def prepare_repository(repository: Repository, staging_root: Path, source_root: Path | None) -> Path:
    destination = staging_root / repository.name
    print(f"Fetching {repository.name} ({repository.ref}, {repository.commit[:12]})", flush=True)
    clone_source = repository.url
    clone_options = ["--filter=blob:none"]
    if source_root is not None:
        local_source = source_root / repository.name
        if not (local_source / ".git").exists():
            raise FetchError(f"Local source repository does not exist: {local_source}")
        clone_source = str(local_source)
        clone_options = ["--no-hardlinks"]
    run(["git", "clone", *clone_options, "--no-checkout", clone_source, str(destination)])
    run(["git", "checkout", "--detach", repository.commit], cwd=destination)
    head = run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True).lower()
    if head != repository.commit:
        raise FetchError(f"Revision mismatch for {repository.name}: expected {repository.commit}, got {head}")
    checkout_state = run(["git", "status", "--porcelain"], cwd=destination, capture=True)
    if checkout_state:
        raise FetchError(f"Incomplete checkout for {repository.name}:\n{checkout_state}")

    for patch in repository.patches:
        print(f"  Applying {patch.relative_to(SCRIPT_DIR)}")
        run(["git", "apply", "--check", str(patch)], cwd=destination)
        run(["git", "apply", str(patch)], cwd=destination)

    for relative_path in repository.remove:
        target = confined_path(destination, relative_path.as_posix(), f"remove path for {repository.name}")
        if target.is_dir():
            remove_tree(target)
        elif target.exists():
            target.unlink()

    for source, relative_destination in repository.overlays:
        target = confined_path(
            destination,
            relative_destination.as_posix(),
            f"overlay destination for {repository.name}",
        )
        apply_overlay(source, target)

    return destination


def install_repositories(
    repositories: list[Repository],
    prepared: dict[str, Path],
    output_root: Path,
    replace: bool,
    backup_root: Path,
) -> None:
    destinations = {repository.name: destination_for(output_root, repository) for repository in repositories}
    existing = [repository for repository in repositories if destinations[repository.name].exists()]
    if existing and not replace:
        names = ", ".join(repository.name for repository in existing)
        raise FetchError(f"Destinations already exist: {names}. Use --replace to back up and replace them.")

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup_session = backup_root / timestamp
    moved_backups: list[tuple[Path, Path]] = []
    installed: list[Path] = []
    try:
        for repository in existing:
            destination = destinations[repository.name]
            backup = backup_session / repository.path
            backup.parent.mkdir(parents=True, exist_ok=True)
            print(f"Backing up {destination} -> {backup}")
            back_up_tree(destination, backup)
            moved_backups.append((destination, backup))

        for repository in repositories:
            destination = destinations[repository.name]
            destination.parent.mkdir(parents=True, exist_ok=True)
            print(f"Installing {repository.name} -> {destination}")
            shutil.copytree(prepared[repository.name], destination)
            installed.append(destination)
    except Exception:
        for destination in reversed(installed):
            if destination.exists():
                remove_tree(destination)
        for destination, backup in reversed(moved_backups):
            if backup.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(backup, destination)
        raise


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="?", choices=("fetch", "check"), default="fetch")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--repo", action="append", default=[], help="Fetch/check only this repository (repeatable)")
    parser.add_argument("--output-root", type=Path, default=SCRIPT_DIR)
    parser.add_argument("--source-root", type=Path, help="Use NAME repositories below this local mirror directory")
    parser.add_argument("--replace", action="store_true", help="Back up and replace existing destinations")
    parser.add_argument("--backup-root", type=Path, default=SCRIPT_DIR / "_component_backups")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        config_path = arguments.config.resolve()
        output_root = arguments.output_root.resolve()
        source_root = arguments.source_root.resolve() if arguments.source_root else None
        backup_root = arguments.backup_root.resolve()
        repositories = select_repositories(load_repositories(config_path), arguments.repo)
        validate_assets(repositories)

        if arguments.command == "check":
            check_repositories(repositories, output_root)
            return 0

        destinations = [destination_for(output_root, repository) for repository in repositories]
        if not arguments.replace and any(destination.exists() for destination in destinations):
            existing = [repository.name for repository, destination in zip(repositories, destinations) if destination.exists()]
            raise FetchError(
                f"Destinations already exist: {', '.join(existing)}. "
                "No files were changed; use --replace to back up and replace them."
            )

        output_root.mkdir(parents=True, exist_ok=True)
        staging_root = Path(tempfile.mkdtemp(prefix=".fetch_repos-", dir=output_root))
        try:
            prepared = {
                repository.name: prepare_repository(repository, staging_root, source_root)
                for repository in repositories
            }
            install_repositories(repositories, prepared, output_root, arguments.replace, backup_root)
        finally:
            if staging_root.exists():
                remove_tree(staging_root)
        print("All selected repositories were installed successfully.")
        return 0
    except (FetchError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
