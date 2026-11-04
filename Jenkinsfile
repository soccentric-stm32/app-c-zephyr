// Jenkinsfile - multi-stage CI for the Zephyr RTOS demo application.
//
//   Checkout -> Toolchain -> Build -> Boot contract -> Image -> Package
//   -> Release (tags) -> Archive
//
// Package builds dist/<repo>-<version>.tar.gz (outputs, manifest.json,
// SHA256SUMS) on every run.  Release runs only for a tag v<version> and
// publishes dist/ as a GitHub release with the Jenkins secret-text credential
// github-release-token.  To release: bump the version, push, then push the
// tag (git tag v0.2.0 && git push origin v0.2.0).
//
// Builds run through scripts/docker-dev.sh (ZCON-001). The Zephyr image is
// built on top of the shared stm32-dev base, so the Toolchain stage creates
// the base first when the agent does not have it yet.

pipeline {
    agent any

    options {
        skipDefaultCheckout()
        timestamps()
        timeout(time: 60, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '30', artifactNumToKeepStr: '10'))
        disableConcurrentBuilds()
    }

    environment {
        BASE_IMAGE  = 'stm32-dev:latest'
        STM32_IMAGE = 'stm32-dev-zephyr:latest'
    }

    stages {
        stage('Checkout') {
            steps {
                checkout scm
                sh '''
                    git log -1 --pretty=fuller
                    echo "commit: $(git rev-parse HEAD)"
                    echo "branch: $(git rev-parse --abbrev-ref HEAD)"
                '''
            }
        }

        stage('Toolchain') {
            steps {
                sh '''
                    docker version --format 'docker server: {{.Server.Version}}'
                    if ! docker image inspect "${BASE_IMAGE}" >/dev/null 2>&1; then
                        echo "Building ${BASE_IMAGE} from docker/Dockerfile.base"
                        docker build -t "${BASE_IMAGE}" -f docker/Dockerfile.base docker
                    fi
                    if ! docker image inspect "${STM32_IMAGE}" >/dev/null 2>&1; then
                        echo "Building ${STM32_IMAGE} from docker/Dockerfile"
                        make image
                    fi
                    docker image inspect --format 'base:  {{.Id}}' "${BASE_IMAGE}"
                    docker image inspect --format 'image: {{.Id}}' "${STM32_IMAGE}"
                    scripts/docker-dev.sh run cmake --version | head -n 1
                '''
            }
        }

        stage('Build') {
            steps {
                sh 'make build'
            }
        }

        stage('Boot contract') {
            steps {
                sh '''
                    python3 - <<'PY'
from pathlib import Path

# The application must stay compatible with bootloader:
# BOOT maps the active bank at 0x08000000, so every image links at the slot
# offset. These values mirror LAY-001..LAY-005 and Appendix B.
expected_defines = {
    "APP_SLOT_BASE": 0x0800C000,
    "APP_SLOT_SIZE": 0x000F4000,
    "APP_META_A_ADDR": 0x08008000,
    "APP_META_B_ADDR": 0x08108000,
    "APP_META_VERIFIED_OFFSET": 0x100,
    "APP_META_ACTIVATED_OFFSET": 0x120,
    "APP_META_CONFIRMED_OFFSET": 0x140,
    "APP_META_REJECTED_OFFSET": 0x160,
}

defines = {}
for line in Path("src/boot_contract.h").read_text(encoding="utf-8").splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[0] == "#define":
        defines[parts[1]] = parts[2]

for name, want in expected_defines.items():
    if name not in defines:
        raise SystemExit(f"boot contract: {name} is missing from src/boot_contract.h")
    got = int(defines[name].rstrip("ULul").rstrip("ULul"), 16)
    if got != want:
        raise SystemExit(f"boot contract: {name} is 0x{got:08X}, expected 0x{want:08X}")

settings = {}
for line in Path("prj.conf").read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, _, value = line.partition("=")
    settings[key.strip()] = value.strip()

expected_settings = {
    "CONFIG_FLASH_LOAD_OFFSET": "0xC000",
    "CONFIG_FLASH_LOAD_SIZE": "0xF4000",
}
for key, want in expected_settings.items():
    got = settings.get(key)
    if got != want:
        raise SystemExit(f"boot contract: {key} is {got!r}, expected {want!r}")

print("boot contract: devicetree offsets and shared constants match BOOT")
PY
                '''
            }
        }

        stage('Image') {
            steps {
                sh '''
                    make size
                    python3 - <<'PY'
import struct
import zlib
from pathlib import Path

version = {}
for line in Path("VERSION").read_text(encoding="utf-8").splitlines():
    if "=" in line:
        key, _, value = line.partition("=")
        version[key.strip()] = value.strip()
want = (int(version["VERSION_MAJOR"]) << 24) | (int(version["VERSION_MINOR"]) << 16) | int(version["PATCHLEVEL"])

path = Path("build/zephyr-app.img")
data = path.read_bytes()
header = struct.unpack_from("<IHHIIIIIII", data, 0)
magic, hver, hsize, itype, ver, isize, icrc, load, entry, _ts = header
assert magic == 0x48574249, "bad image magic"
assert hver == 1, f"unexpected header version {hver}"
assert hsize == 256, f"unexpected header size {hsize}"
assert itype == 1, f"unexpected image type {itype}"
assert ver == want, f"image version 0x{ver:08X} does not match VERSION 0x{want:08X}"
assert load == 0x0800C000, f"image loads at 0x{load:08X}, expected 0x0800C000"
assert len(data) == hsize + isize, "image size does not match the header"
assert zlib.crc32(data[hsize:]) & 0xFFFFFFFF == icrc, "image CRC32 mismatch"
sp = struct.unpack_from("<I", data, hsize)[0]
assert 0x20000000 < sp <= 0x20080000, "initial stack pointer is outside internal RAM"
entry_ok = entry & 1 and 0x0800C000 <= (entry & ~1) < 0x0800C000 + 0x000F4000
assert entry_ok, "reset vector is outside the slot"
print(f"image: {path} v{(ver >> 24) & 0xFF}.{(ver >> 16) & 0xFF}.{ver & 0xFF} "
      f"({isize} bytes payload, entry 0x{entry:08X})")
PY
                '''
            }
        }

        stage('Package') {
            steps {
                sh '''
                    python3 ci/package.py --name app-c-zephyr --version "$(ci/version.sh)" \\
                        build/zephyr-app.elf build/zephyr-app.bin build/zephyr-app.hex build/zephyr-app.map build/zephyr-app.img \\
                        README.md LICENSE
                '''
            }
        }

        stage('Release') {
            // Only for a tag build (Multibranch job with tag discovery).  The
            // tag must be v<version>; ci/github_release.py checks it.
            when { buildingTag() }
            steps {
                withCredentials([string(credentialsId: 'github-release-token', variable: 'GITHUB_TOKEN')]) {
                    sh 'python3 ci/github_release.py --tag "${TAG_NAME}" --version "$(ci/version.sh)" dist/*'
                }
            }
        }

        stage('Archive') {
            steps {
                archiveArtifacts artifacts: 'dist/*', fingerprint: true
            }
        }
    }

    post {
        always {
            deleteDir()
        }
        success {
            echo 'Zephyr application: build, boot-contract and image checks passed.'
        }
        failure {
            echo 'Zephyr application: pipeline failed - see the stage logs above.'
        }
    }
}
