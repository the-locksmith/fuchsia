#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime
import json
import os
import subprocess
import sys

TERM_COLOR_RED = '\033[91m'
TERM_COLOR_END = '\033[0m'

ROOT_PATH = os.path.abspath(__file__ + "/../../..")
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml

# List of packages that are part of the third-party build
# that live in-tree. In order to unify the two packages in builds
# that use the output cargo.tomls, we use `= "*"` as the version
# for these libraries, causing them to resolve via the patch section.
IN_TREE_THIRD_PARTY_PACKAGES = [
]

CARGO_TOML_CONTENTS = '''\
# Copyright %(year)s The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

[package]
name = "%(package_name)s"
version = "%(version)s"
edition = "%(edition)s"
license-file = "LICENSE"
authors = ["rust-fuchsia@fuchsia.com"]
description = "Rust crate for Fuchsia OS"
repository = "https://fuchsia.googlesource.com"

%(bin_or_lib)s
%(is_proc_macro)s
name = "%(crate_name)s"%(lib_crate_type)s
path = "%(source_root)s"
'''

def cur_year():
    return datetime.datetime.now().year

def main():
    parser = argparse.ArgumentParser("Writes a Cargo.toml for a Rust crate")
    parser.add_argument("--package-name",
                        help="Name of the package",
                        required=True)
    parser.add_argument("--crate-name",
                        help="Name of the crate",
                        required=True)
    parser.add_argument("--source-root",
                        help="Root lib.rs or main.rs for the crate",
                        required=True)
    parser.add_argument("--version",
                        help="Version of crate",
                        required=True)
    parser.add_argument("--edition",
                        help="Edition of rust to use when compiling the crate",
                        required=True)
    parser.add_argument("--out-dir",
                        help="Path to directory where Cargo.toml should be written",
                        required=True)
    parser.add_argument("--crate-type",
                        help="Type of crate to build",
                        required=True,
                        choices=["bin", "rlib", "staticlib", "proc-macro"])
    parser.add_argument("--lto",
                        help="Add lto options to crate",
                        required=False,
                        choices=["none", "thin", "fat"])
    parser.add_argument("--third-party-deps-data",
                        help="Path to output of third_party_crates.py",
                        required=True)
    parser.add_argument("--dep-data",
                        action="append",
                        help="Path to metadata from a previous invocation of this script",
                        required=False)

    parser.add_argument
    args = parser.parse_args()
    cargo_toml_path = os.path.join(args.out_dir, "Cargo.toml")

    third_party_json = json.load(open(args.third_party_deps_data))

    deps = {}
    if args.dep_data:
        for data_path in args.dep_data:
            if not os.path.isfile(data_path):
                print TERM_COLOR_RED
                print "Missing Rust target data for dependency " + data_path
                print "Did you accidentally depend on a non-Rust target?"
                print TERM_COLOR_END
                return -1
            dep_data = json.load(open(data_path))
            if dep_data["third_party"]:
                package_name = dep_data["package_name"]
                if package_name not in third_party_json["crates"]:
                    print(TERM_COLOR_RED)
                    print("Missing Rust target dependency:" + data_path)
                    print("Package is present in the third_party/ but is absent"
                          " from dependency data: " + args.third_party_deps_data)
                    print("Maybe this package is conditionally disabled for the"
                          " current configuration of '%s'?" % args.crate_name)
                    print(TERM_COLOR_END)
                    return -1
                crate_data = third_party_json["crates"][package_name]
                deps[package_name] = crate_data["cargo_dependency_toml"]
            else:
                package_name = dep_data["package_name"]
                if package_name in IN_TREE_THIRD_PARTY_PACKAGES:
                    deps[package_name] = {
                        "version": "*",
                    }
                else:
                    deps[package_name] = {
                        "path": dep_data["cargo_toml_dir"],
                        "version": dep_data["version"],
                    }

    with open(cargo_toml_path, "w") as file:
        file.write(CARGO_TOML_CONTENTS % {
            "package_name": args.package_name,
            "crate_name": args.crate_name,
            "version": args.version,
            "edition": args.edition,
            "deps": deps,
            "year": cur_year(),
            "bin_or_lib": "[[bin]]" if args.crate_type == "bin" else "[lib]",
            "is_proc_macro": "proc-macro = true" if args.crate_type == "proc-macro" else "",
            "lib_crate_type": "" if args.crate_type == "bin" else (
                '\ncrate_type = ["%s"]' % args.crate_type
            ),
            "source_root": args.source_root,
        })
        dependencies = { "dependencies": deps }
        file.write(pytoml.dumps({
            "dependencies": deps,
            "patch": { "crates-io": third_party_json["patches"] },
        }))

        profile = { "profile": { "release": { "panic" : "abort", "opt-level": "z" } } }
        if args.lto and args.lto != "none":
            profile["profile"]["release"]["lto"] = args.lto

        file.write(pytoml.dumps(profile))

if __name__ == '__main__':
    sys.exit(main())
