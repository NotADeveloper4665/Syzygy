#!/usr/bin/env bash
# GitHub Actions only: publish an artifact from a successful trusted main build.
set -Eeuo pipefail
rpm_path="${1:?RPM path required}"
: "${GITHUB_REPOSITORY:?}" "${SYZYGY_DEV_RUN:?}" "${SYZYGY_DEV_COMMIT:?}"
[[ "$SYZYGY_DEV_RUN" =~ ^[0-9]+$ && "$SYZYGY_DEV_COMMIT" =~ ^[0-9a-f]{40}$ ]]
[[ -f "$rpm_path" && "$(basename "$rpm_path")" == Syzygy.rpm ]]
# Serial publisher jobs select the newest successful build. Skip an already
# published or newer run, including after a delayed workflow_run event.
if gh release download dev --pattern dev-build.json --dir previous-dev >/dev/null 2>&1; then
  previous_run="$(python3 -c 'import json; print(json.load(open("previous-dev/dev-build.json"))["run_id"])')"
  if [[ "$previous_run" =~ ^[0-9]+$ ]] && ((previous_run >= SYZYGY_DEV_RUN)); then
    echo "Development release already contains this build or a newer validated run."
    exit 0
  fi
fi
python3 - "$rpm_path" <<'PY'
import hashlib, json, os, sys
from pathlib import Path
package = Path(sys.argv[1])
manifest = {'schema': 1, 'rpm': 'Syzygy.rpm', 'fedora': 44, 'arch': 'x86_64',
            'commit': os.environ['SYZYGY_DEV_COMMIT'], 'run_id': os.environ['SYZYGY_DEV_RUN'],
            'sha256': hashlib.sha256(package.read_bytes()).hexdigest()}
Path('dev-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY
notes="Validated main build $SYZYGY_DEV_COMMIT. Fedora 44 x86_64. Update with scripts/update-syzygy-dev-rpm.sh. Build: https://github.com/$GITHUB_REPOSITORY/actions/runs/$SYZYGY_DEV_RUN"
if gh release view dev >/dev/null 2>&1; then
  gh api --method PATCH "repos/$GITHUB_REPOSITORY/git/refs/tags/dev" -f sha="$SYZYGY_DEV_COMMIT" -F force=true
  gh release edit dev --title "Syzygy development RPM" --prerelease --latest=false --notes "$notes"
else
  gh release create dev --target "$SYZYGY_DEV_COMMIT" --title "Syzygy development RPM" --prerelease --latest=false --notes "$notes"
fi
gh release upload dev "$rpm_path" --clobber
# Update the manifest last. Mixed asset downloads fail verification and retry.
gh release upload dev dev-build.json --clobber
