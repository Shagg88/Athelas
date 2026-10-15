# Sourced by the build scripts with VERSION_FILE set: the VERSION file (one line, e.g. 1.0.0 or 0.8.0-test89)
# -> ROTKFIX_VERSION (shown in the launcher and the log), VERSION_RC (1,0,0,0 for the exe's file properties)
# and VDEF (the compiler flag, a single word: the version has no spaces).
ROTKFIX_VERSION=$(tr -d ' \r\n' < "$VERSION_FILE")
VERSION_RC=$(echo "${ROTKFIX_VERSION%%-*}" | awk -F. '{printf "%d,%d,%d,0", $1, $2, $3}')
VDEF="-DROTKFIX_VERSION=\"$ROTKFIX_VERSION\""
