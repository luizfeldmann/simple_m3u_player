#! /bin/sh

cp player.service /lib/systemd/system/
systemctl enable player
systemctl start player
