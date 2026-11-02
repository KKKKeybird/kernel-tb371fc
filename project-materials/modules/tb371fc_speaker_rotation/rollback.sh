#!/system/bin/sh
M=${0%/*}
touch "$M/disable"
echo 'Module disabled for next boot. To restore the accepted v0.12 setup, restore BOTH its module backup and boot image after explicit reboot authorization.'
