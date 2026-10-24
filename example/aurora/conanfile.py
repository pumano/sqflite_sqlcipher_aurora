from conan import ConanFile


class Pkg(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "PkgConfigDeps"

    def requirements(self):
        self.requires("sqlcipher/4.6.1@aurora")
        # The prebuilt sqlcipher binaries on the Aurora conan remote were built
        # against different OpenSSL versions per architecture. Pin the transitive
        # OpenSSL to the version each sqlcipher binary was linked against,
        # otherwise conan resolves the latest OpenSSL and the package-id no
        # longer matches any prebuilt binary ("Missing binary").
        if self.settings.arch == "armv8":
            self.requires("openssl/1.1.1w@aurora", override=True)
        else:
            self.requires("openssl/3.3.3@aurora", override=True)
