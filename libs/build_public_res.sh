#!/bin/bash
set -e

source libs/env_deploy.sh
DEST=$DEPLOYMENT/public_res
rm -rf $DEST
mkdir -p $DEST

#### Download geodata ####
curl -fLso $DEST/geoip.dat "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geoip.dat"
curl -fLso $DEST/geosite.dat "https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat"
curl -fLso $DEST/geoip.db "https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip.db"
curl -fLso $DEST/geosite.db "https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite.db"

#### Rule-sets for the new core (geosite:xxx / geoip:xx in rules) ####
mkdir -p $DEST/rule-set
for repo in sing-geosite sing-geoip; do
  rm -rf /tmp/$repo-rule-set
  git clone -q --depth 1 -b rule-set https://github.com/SagerNet/$repo.git /tmp/$repo-rule-set
  cp /tmp/$repo-rule-set/*.srs $DEST/rule-set/
done
test -f $DEST/rule-set/geoip-ru.srs
test -f $DEST/rule-set/geosite-category-ru.srs

#### copy res/public ####
cp res/public/* $DEST
