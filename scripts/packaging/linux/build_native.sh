#!/bin/sh
# Builds the .rpm and Arch packages from the recipes package_linux.py writes
# (ADR-0012, NOW row 72):
#
#   build_native.sh OUT EPOCH   # after package_linux.py --out OUT --formats rpm,arch
#
# OUT/rpm/kisakcod.spec needs rpmbuild; OUT/arch/PKGBUILD needs makepkg. Each
# is skipped when its recipe is absent. The packages land in OUT.
set -eu
out=$(CDPATH= cd "$1" && pwd)  # rpmbuild and makepkg change directory
epoch=$2
SOURCE_DATE_EPOCH=$epoch
export SOURCE_DATE_EPOCH

if [ -f "$out/rpm/kisakcod.spec" ]; then
    arch=$(sed -n 's/^BuildArch: //p' "$out/rpm/kisakcod.spec")
    rpmbuild -bb --target "$arch" --define "_topdir $out/rpm" \
        --define 'source_date_epoch_from_changelog 0' "$out/rpm/kisakcod.spec"
    mv "$out/rpm/RPMS/$arch"/*.rpm "$out/"
fi

if [ -f "$out/arch/PKGBUILD" ]; then
    (cd "$out/arch" && PKGDEST=$out PKGEXT=.pkg.tar.zst \
        CARCH=$(sed -n "s/^arch=('\(.*\)')$/\1/p" PKGBUILD) \
        makepkg --force --nodeps --noconfirm)
fi
