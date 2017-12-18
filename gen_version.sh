#!/bin/sh

stem=rawspec
STEM=RAWSPEC

VF=${stem}_version.h

# Requires that version tags are annotated and start with v#
VN=$(git describe --match "v[0-9]*" --abbrev=7 HEAD 2>/dev/null)

# Redirect stdout to stderr
git update-index -q --refresh 1>&2
test -z "$(git diff-index --name-only HEAD --)" || {
  VN="$VN-dirty"
}

VN=$(echo "$VN" | sed -e 's/-/+/');
VN=$(echo "$VN" | sed -e 's/-/@/');

# Strip leading "v"(s)
VN=$(expr "$VN" : v*'\(.*\)')

if test -r $VF
then
  VC=$(sed -e "s/^#define ${STEM}_VERSION //" <$VF)
else
  VC=unset
fi

test "$VN" = "$VC" || {
  echo >&2 "#define ${STEM}_VERSION $VN"
  echo "#define ${STEM}_VERSION $VN" >$VF
}
