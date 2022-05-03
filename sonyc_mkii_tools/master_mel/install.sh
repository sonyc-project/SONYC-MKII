#!/usr/bin/env bash

if [ "$1" = 'uninstall' ]; then
  echo 'uninstalling mastermel'
  systemctl stop mastermel
  systemctl disable mastermel
  rm /etc/systemd/system/mastermel.service
  #rm /usr/lib/systemd/system/mastermel.service
  systemctl daemon-reload
  systemctl reset-failed
  exit 0
fi

# build
echo "build the master_mel executable"
# DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd )"
# cd "$DIR"
make
cp master_mel /usr/local/bin/
cp mastermel-run.sh /usr/local/bin/

# start
echo 'copying mastermel...'
cp mastermel.service /etc/systemd/system/
mkdir -p /home/root/.mastermel
echo 'starting reconnect and setting to run on boot...'
systemctl daemon-reload
systemctl start mastermel
systemctl enable mastermel
echo 'done.'
sleep 1
systemctl status mastermel
echo 'To see logs:  journalctl -u mastermel'
# cd -
exit 0
