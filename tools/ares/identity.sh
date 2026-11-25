# SPDX-License-Identifier: MIT
# One pinned source and ordered patch set for provisioning and slot admission.
ARES_REV=d9117003192cd43806ddc24d20c764ddb9afc965
ARES_COOP_PATCHES='local-tree sram-persistence two-pad'

sampled_ares_identity() {
 local path=$1 p digest
 {
  printf '%s\n' "$ARES_REV"
  for p in $ARES_COOP_PATCHES; do
   digest=$(rtk proxy sha256sum -- "$path/$p.patch") || return
   printf '%s %s\n' "$p" "${digest%% *}"
  done
 } | rtk proxy sha256sum | rtk cut -d' ' -f1
}
