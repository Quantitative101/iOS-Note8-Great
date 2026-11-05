#!/bin/bash
# Build the iOS 16.7.16 root volume (HFSX, partition-less) for the Note 8 from the iPhone X IPSW.
# Local version of the S7 repo's .github/workflows/ios-root.yml. Run it yourself in a Terminal:
#     bash ~/MakeiOSGreatAgain/note8/build-root.sh
# It asks for your administrator password once (the staging scripts must run as root so the iOS
# UIDs/GIDs are right). Needs about 45 GB of free disk. Output: note8/root-build/root.img (20 GiB raw)
# and root.img.sha256. Nothing is written to the phone. The image contains Apple software: keep it local.
set -euo pipefail

SIZE_MB=${SIZE_MB:-20480}
NOTE8=$HOME/MakeiOSGreatAgain/note8
IPSW=$HOME/MakeiOSGreatAgain/ref/ipsw-x/iPhone10,3_16.7.16_20H392_Restore.ipsw
TOOLS=$HOME/MakeiOSGreatAgain/ref/a5xelte-herolte/repo/tools
W=$NOTE8/root-build

[[ -f "$IPSW" ]] || { echo "missing IPSW: $IPSW" >&2; exit 1; }
mkdir -p "$W/ipsw"
cd "$W"
echo "Free disk:"; df -h "$W" | tail -1

echo "== administrator password (needed once) =="
sudo -v

echo "== 1/6 extract IPSW members =="
unzip -p "$IPSW" BuildManifest.plist > ipsw/BuildManifest.plist
python3 "$NOTE8/tools/ipsw-members.py" ipsw/BuildManifest.plist > members.txt
cat members.txt
while read -r name path; do
    if [[ ! -f "ipsw/$name.dmg" ]]; then
        unzip -o -j "$IPSW" "$path" -d ipsw
        mv "ipsw/${path##*/}" "ipsw/$name.dmg"
    fi
done < members.txt
build=$(/usr/libexec/PlistBuddy -c 'Print :ProductBuildVersion' ipsw/BuildManifest.plist)
ls -l ipsw

attached=()
cleanup() {
    for m in "${attached[@]:-}"; do [[ -n "$m" ]] && hdiutil detach "$m" >/dev/null 2>&1 || true; done
}
trap cleanup EXIT

attach() {  # attach DMG -> mount point (read-only, owners honoured)
    hdiutil attach -nobrowse -readonly -owners on "$1" | awk -F'\t' '/\/Volumes\// {print $NF}' | tail -1
}

echo "== 2/6 mount the three IPSW disk images =="
os=$(attach ipsw/os.dmg); attached+=("$os")
sys=$(attach ipsw/systemos.dmg); attached+=("$sys")
app=$(attach ipsw/appos.dmg); attached+=("$app")
echo "OS=$os SystemOS=$sys AppOS=$app"

echo "== 3/6 create the ${SIZE_MB} MiB HFSX volume =="
rm -f root.sparseimage
hdiutil create -size "${SIZE_MB}m" -type SPARSE -layout NONE -fs "Case-sensitive Journaled HFS+" -volname IOSROOT root
root=$(hdiutil attach -nobrowse -owners on root.sparseimage | awk -F'\t' '/\/Volumes\// {print $NF}' | tail -1)
attached+=("$root")
echo "root=$root"

echo "== 4/6 stage and tune the root (as root) =="
sudo env STAGE_ALL_APPS=1 STAGE_FULL=1 bash "$TOOLS/a5xelte-stage-ios-root.sh" "$os" "$sys" "$app" "$root"
sudo bash "$TOOLS/herolte-tune-ios-root.sh" "$root" "$build"
df -h "$root"

echo "== 5/6 unmount =="
sync
hdiutil detach "$root"; hdiutil detach "$os"; hdiutil detach "$sys"; hdiutil detach "$app"
attached=()
rm -f ipsw/os.dmg ipsw/systemos.dmg ipsw/appos.dmg

echo "== 6/6 raw image =="
dev=$(hdiutil attach -nomount -nobrowse root.sparseimage | awk 'NR==1 {print $1}')
echo "device $dev"
attached+=("$dev")
sudo dd if="/dev/r${dev#/dev/}" of=root.img bs=4m
hdiutil detach "$dev"
attached=()
shasum -a 256 root.img | tee root.img.sha256
ls -l root.img
echo "DONE: $W/root.img ($(du -h root.img | cut -f1) on disk). You can delete root.sparseimage."
