#!/usr/bin/env python3
"""Compile/run our lifecycle helpers; optionally compile changed AA source
against a normalized copy of the actual P2873 stock API. No MMI execution.
CC BY-NC-SA 4.0; see LICENSE.md.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "lsd/de/audi/app/terminalmode/smartphone/androidauto2"
ECJ_SHA512 = "ab441acf5551a7dc81c353eaccb3b3df9e89a48987294d19e39acdb83a5b640fcdff7414cee29f5b96eaa8826647f1d5323e185018fe33a64c402d69c73c9158"


def run(command):
    result = subprocess.run([str(p) for p in command], capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    if result.stdout.strip():
        print(result.stdout.strip())


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--java-home", type=Path, default=Path("C:/Program Files/Java/jdk-23"))
    p.add_argument("--ecj", type=Path, default=ROOT.parent / "mib2p-android-auto-cluster/android_auto/mh2p/build-java-tools/ecj-4.20.jar")
    p.add_argument("--stock-api", type=Path)
    p.add_argument("--compile-integration", action="store_true")
    p.add_argument("--native-peer", type=Path)
    args = p.parse_args()
    if hashlib.sha512(args.ecj.read_bytes()).hexdigest() != ECJ_SHA512:
        raise ValueError("Eclipse compiler checksum differs")
    build = ROOT / "build-local/java"
    build.mkdir(parents=True, exist_ok=True)
    classes = build / "classes"
    classes.mkdir(exist_ok=True)
    java = args.java_home / "bin/java.exe"
    javac = args.java_home / "bin/javac.exe"
    sources = [PACKAGE / "nav/ClusterLifecycle.java", PACKAGE / "nav/ClusterLifecycleFiles.java"]
    if args.compile_integration:
        if not args.stock_api:
            raise ValueError("--compile-integration requires --stock-api")
        # Audi's no-op dependency is retained in the upstream AA-only archive.
        stub = build / "audi-stub.jar"
        with zipfile.ZipFile(ROOT / "builds/ClusterIntegration_v0034_beta2_candidate_90d0b76_audi.zip") as z:
            name = next(n for n in z.namelist() if n.endswith(".jar") and "stub" in n.lower())
            stub.write_bytes(z.read(name))
        sources += [PACKAGE / "nav/AndroidAutoClusterIntegration.java", PACKAGE / "AndroidAuto2Subsystem.java"]
    command = [java, "-jar", args.ecj, "-source", "1.4", "-target", "1.4", "-encoding", "UTF-8", "-nowarn", "-d", classes]
    if args.stock_api:
        command += ["-bootclasspath", args.stock_api]
    if args.compile_integration:
        command += ["-classpath", stub]
    run(command + sources)
    for path in (classes / "de").rglob("*.class"):
        if struct.unpack(">H", path.read_bytes()[6:8])[0] != 48:
            raise ValueError("Not Java 1.4 bytecode: " + str(path))
    host = build / "host-tests"
    host.mkdir(exist_ok=True)
    run([javac, "-cp", classes, "-d", host, ROOT / "tests/ClusterLifecycleTest.java"])
    run([java, "-Xverify:all", "-cp", os.pathsep.join((str(host), str(classes))), "ClusterLifecycleTest", build / "files"])
    if args.native_peer:
        run([javac, "-cp", classes, "-d", host, ROOT / "tests/ClusterLifecycleProcessTest.java"])
        run([java, "-Xverify:all", "-cp", os.pathsep.join((str(host), str(classes))), "ClusterLifecycleProcessTest", args.native_peer.resolve()])
    print("Changed Java source compiled against " + (str(args.stock_api) if args.stock_api else "host APIs") + "; production classes major48")


if __name__ == "__main__":
    main()
