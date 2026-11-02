# Source from Bash: source scripts/env.sh
ROCOS_COMM_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$ROCOS_COMM_ROOT/work/deps-src/fastddsgen/scripts:$PATH"
export CMAKE_PREFIX_PATH="$ROCOS_COMM_ROOT/install:$ROCOS_COMM_ROOT/work/deps-install${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
# Optional locally extracted JDK; normally install JDK 17 using the OS package manager.
if [[ -z "${JAVA_HOME:-}" && -x "$ROCOS_COMM_ROOT/work/jdk/usr/lib/jvm/java-17-openjdk-amd64/bin/java" ]]; then
    export JAVA_HOME="$ROCOS_COMM_ROOT/work/jdk/usr/lib/jvm/java-17-openjdk-amd64"
fi
if [[ -n "${JAVA_HOME:-}" ]]; then
    export PATH="$JAVA_HOME/bin:$PATH"
fi
