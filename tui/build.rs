//! Links the CMake-built backend: libgemstats.a (C++), libgem.a (C), and
//! their runtime dependencies. GEM_BUILD_DIR overrides ../build.
use std::{env, path::PathBuf};

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let build = env::var("GEM_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| manifest.join("../build"));
    let build = build.canonicalize().unwrap_or(build);
    for lib in ["libgemstats.a", "libgem.a"] {
        let path = build.join(lib);
        assert!(path.exists(), "{} missing: build the backend first (cmake --build build)", path.display());
        println!("cargo:rerun-if-changed={}", path.display());
    }
    println!("cargo:rerun-if-env-changed=GEM_BUILD_DIR");
    println!("cargo:rustc-link-search=native={}", build.display());
    println!("cargo:rustc-link-lib=static=gemstats");
    println!("cargo:rustc-link-lib=static=gem");
    println!("cargo:rustc-link-lib=dylib=stdc++");
    println!("cargo:rustc-link-lib=dylib=crypto");
    println!("cargo:rustc-env=GEM_DEFAULT_CORPUS={}", manifest.join("../out/wlc.gemc").display());
}
