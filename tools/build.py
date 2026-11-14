#!/usr/bin/env python3
"""Build with the experimental DataStore/event engine checkout and its prepared SDK."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--defold-source", type=Path, default=os.environ.get("DEFOLD_SOURCE"))
    parser.add_argument("--sdk", type=Path, default=os.environ.get("DYNAMO_HOME"))
    parser.add_argument("--bob", type=Path, default=os.environ.get("BOB_JAR"))
    parser.add_argument("--platform", default="arm64-macos" if platform.machine() == "arm64" else "x86_64-macos")
    parser.add_argument("--graphics", choices=("webgpu", "webgl"), default="webgpu", help="Browser graphics backend")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--content-only", action="store_true")
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--offscreen", action="store_true", help="Build a hidden engine and render to a texture")
    args = parser.parse_args()
    web = args.platform == "wasm-web"
    webgpu = web and args.graphics == "webgpu"
    if web and (args.offscreen or args.test):
        parser.error("use a native platform for --offscreen or --test")
    if not args.sdk:
        parser.error("provide --sdk or DYNAMO_HOME for a prepared engine SDK")
    sdk = args.sdk.expanduser().resolve()
    default_bob = sdk / "share/java/bob-light.jar"
    if not default_bob.is_file():
        default_bob = sdk / "share/java/bob.jar"
    bob = (args.bob or default_bob).expanduser().resolve()
    if not bob.is_file():
        parser.error(f"Bob not found: {bob}")
    engine_name = "dmengine_offscreen" if args.offscreen else "dmengine"
    build = ROOT / "build"
    if web:
        os.environ.setdefault("EM_CACHE", str(build / "emscripten-cache"))
        os.environ.setdefault("EM_PORTS", str(build / "emscripten-ports"))
    content = build / ("content-src-wasm-web" if web else "content-src")
    content.mkdir(parents=True, exist_ok=True)
    compiler_stamp = content / ".compiler_stamp"
    compiler_id = f"{bob}:{bob.stat().st_mtime_ns}:{args.graphics if web else 'native'}"
    if not compiler_stamp.exists() or compiler_stamp.read_text() != compiler_id:
        shutil.rmtree(content / "build", ignore_errors=True)
        shutil.rmtree(content / ".internal", ignore_errors=True)
        compiler_stamp.write_text(compiler_id)
    source = args.defold_source.expanduser().resolve() if args.defold_source else None
    if source:
        shutil.copytree(source / "engine/engine/content/builtins", content / "builtins", dirs_exist_ok=True)
        shutil.copytree(source / "engine/font/content/builtins", content / "builtins", dirs_exist_ok=True)
    elif (sdk / "content/builtins").is_dir():
        shutil.copytree(sdk / "content/builtins", content / "builtins", dirs_exist_ok=True)
    # Exclude the native extension from Bob's upload discovery; CMake links it locally.
    for directory in ("main", "render", "input", "web", "models"):
        shutil.copytree(ROOT / directory, content / directory, dirs_exist_ok=True)
    shutil.copy2(ROOT / "game.project", content / "game.project")
    bob_args = ["java", "-jar", bob, "--root", content, "--output", "build/default", "--platform", args.platform,
                "--max-cpu-threads", "4", "--debug-output-msl", "true"]
    if web:
        bob_args.extend(["--architectures", "wasm-web"])
    if webgpu:
        bob_args.extend(["--debug-output-wgsl", "true"])
    run([*bob_args, *(["--archive"] if web else []), "build"])
    content_output = build / "wasm-web/content" if web else build / "default"
    shutil.copytree(content / "build/default", content_output, dirs_exist_ok=True)
    if not args.content_only:
        if not args.defold_source:
            parser.error("provide --defold-source or DEFOLD_SOURCE for the DataStore/event checkout")
        source = args.defold_source.expanduser().resolve()
        if not (source / "engine/datastore/src/dmsdk/datastore/datastore.h").is_file():
            parser.error("this engine checkout does not contain DataStore")
        local_sdk = build / ("sdk-wasm-web" if web else "sdk")
        local_sdk.mkdir(exist_ok=True)
        # Header targets write into the SDK. Isolate these from the supplied SDK.
        for directory in ("sdk", "include"):
            if not (local_sdk / directory).exists():
                shutil.copytree(sdk / directory, local_sdk / directory)
        # Private headers must match the engine sources, including experimental APIs.
        # Keep generated protocol headers from the prepared SDK until CMake regenerates them.
        for header in (local_sdk / "include").rglob("*"):
            if not header.is_file() or header.suffix not in (".h", ".hpp"):
                continue
            relative = header.relative_to(local_sdk / "include")
            if len(relative.parts) < 2:
                continue
            module, rest = relative.parts[0], Path(*relative.parts[1:])
            candidates = [source / "engine" / module / "src" / rest,
                          source / "engine" / module / "src" / module / rest]
            if module == "font" and rest.parts[0] == "internal":
                candidates.append(source / "engine/font/src/render" / rest.relative_to("internal"))
            for candidate in candidates:
                if candidate.is_file():
                    if candidate.read_bytes() != header.read_bytes():
                        shutil.copy2(candidate, header)
                    break
        if web:
            # Web targets stage JS libraries during configure; keep those writes local.
            for directory in ("ext", "lib"):
                dest = local_sdk / directory
                if dest.is_symlink():
                    dest.unlink()
                dest.mkdir(exist_ok=True)
            for directory in ("lib", "ext/lib"):
                dest = local_sdk / directory
                dest.mkdir(exist_ok=True)
                for entry in (sdk / directory).iterdir():
                    target = dest / entry.name
                    if entry.name == args.platform:
                        shutil.copytree(entry, target, dirs_exist_ok=True)
                    elif not target.exists():
                        target.symlink_to(entry, target_is_directory=entry.is_dir())
            for entry in (sdk / "ext").iterdir():
                target = local_sdk / "ext" / entry.name
                if entry.name != "lib" and not target.exists():
                    target.symlink_to(entry, target_is_directory=entry.is_dir())
        for directory in (("share", "bin") if web else ("ext", "share", "lib", "bin")):
            dest = local_sdk / directory
            if not dest.exists():
                dest.symlink_to(sdk / directory, target_is_directory=True)
        if web:
            bullet_names = ("BulletDynamics", "BulletCollision", "LinearMath")
            bullet_libs = local_sdk / "ext/lib/wasm-web"
            if not all((bullet_libs / ("lib" + name + ".a")).is_file() for name in bullet_names):
                bullet = build / "bullet-wasm-web"
                run(["cmake", "-S", source / "external/bullet3d", "-B", bullet, "-G", "Ninja",
                     f"-DDEFOLD_SDK_ROOT={local_sdk}", "-DTARGET_PLATFORM=wasm-web",
                     f"-DDEFOLD_BUILD_HOME={bullet / 'defold-build'}"])
                run(["cmake", "--build", bullet, "--target", *bullet_names, "-j", args.jobs])
                for name in bullet_names:
                    shutil.copy2(bullet / name / ("lib" + name + ".a"), bullet_libs)
        native = build / ("native-wasm-web" if web else "native")
        run(["cmake", "-S", ROOT, "-B", native, "-G", "Ninja", f"-DDEFOLD_SOURCE={source}",
             f"-DDEFOLD_SDK_ROOT={local_sdk}", f"-DTARGET_PLATFORM={args.platform}",
             f"-DMOONBASE_OFFSCREEN={'ON' if args.offscreen else 'OFF'}",
             f"-DWITH_WEBGPU={'ON' if webgpu else 'OFF'}"])
        targets = ["dmengine"]
        if args.test:
            targets.append("moonbase_tests")
        run(["cmake", "--build", native, "--target", *targets, "-j", args.jobs])
        output = build / args.platform / ("dmengine.js" if web else engine_name)
        output.parent.mkdir(parents=True, exist_ok=True)
        native_output = native / f"defold-build/engine/engine/build/{args.platform}"
        if web:
            for suffix in (".js", ".wasm", ".js.symbols"):
                shutil.copy2(native_output / ("dmengine" + suffix), output.parent / ("dmengine" + suffix))
        else:
            temporary = output.with_name(output.name + ".new")
            shutil.copy2(native_output / "dmengine", temporary)
            temporary.replace(output)
        if args.test:
            run([native / "moonbase_tests"])
    if web and not args.content_only:
        bundle_args = [*bob_args, "--binary-output", build, "--bundle-output", build / "web",
                       "--variant", "debug", "bundle"]
        run(bundle_args)
        if webgpu:
            # Resolve our HTML locally before setting the adapter manifest. Bob
            # then uses that HTML and includes its WebGPU support probe in dmloader.
            manifest_dir = content / "build/wasm-web"
            manifest_dir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(build / "wasm-web/manifests/web/index.html", manifest_dir / "engine_template.html")
            with (content / "game.project").open("a") as project:
                project.write("\n[native_extension]\napp_manifest = /web/webgpu.appmanifest\n")
            run(bundle_args)
        print(f"Web bundle: {build / 'web'}")
    if args.run and web:
        run([sys.executable, ROOT / "tools/run_web.py"], cwd=ROOT)
    elif args.run:
        flags = ["--config=moonbase.offscreen=1", "--config=display.focus_on_show=0"] if args.offscreen else []
        run([build / args.platform / engine_name, *flags, build / "default/game.projectc"], cwd=ROOT)


if __name__ == "__main__":
    main()
