trap 'onExit $?' EXIT

onExit() {
  if [ "$1" != "0" ]; then
    echo -e "\e[31m"
    echo " ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄ "
    echo "▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌"
    echo "▐░█▀▀▀▀▀▀▀▀▀ ▐░█▀▀▀▀▀▀▀█░▌▐░█▀▀▀▀▀▀▀█░▌▐░█▀▀▀▀▀▀▀█░▌▐░█▀▀▀▀▀▀▀█░▌"
    echo "▐░▌          ▐░▌       ▐░▌▐░▌       ▐░▌▐░▌       ▐░▌▐░▌       ▐░▌"
    echo "▐░█▄▄▄▄▄▄▄▄▄ ▐░█▄▄▄▄▄▄▄█░▌▐░█▄▄▄▄▄▄▄█░▌▐░▌       ▐░▌▐░█▄▄▄▄▄▄▄█░▌"
    echo "▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░░░░░░░░░░▌"
    echo "▐░█▀▀▀▀▀▀▀▀▀ ▐░█▀▀▀▀█░█▀▀ ▐░█▀▀▀▀█░█▀▀ ▐░▌       ▐░▌▐░█▀▀▀▀█░█▀▀ "
    echo "▐░▌          ▐░▌     ▐░▌  ▐░▌     ▐░▌  ▐░▌       ▐░▌▐░▌     ▐░▌  "
    echo "▐░█▄▄▄▄▄▄▄▄▄ ▐░▌      ▐░▌ ▐░▌      ▐░▌ ▐░█▄▄▄▄▄▄▄█░▌▐░▌      ▐░▌ "
    echo "▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░▌       ▐░▌▐░░░░░░░░░░░▌▐░▌       ▐░▌"
    echo " ▀▀▀▀▀▀▀▀▀▀▀  ▀         ▀  ▀         ▀  ▀▀▀▀▀▀▀▀▀▀▀  ▀         ▀ "
    echo "                                                                 "
    echo -e "\e[0m"
  fi
}

onSuccess() {
  echo -e "\e[92m\n"
  echo ""
  echo "   ██████  ██   ██ ██ "
  echo "  ██    ██ ██  ██  ██ "
  echo "  ██    ██ █████   ██ "
  echo "  ██    ██ ██  ██     "
  echo "   ██████  ██   ██ ██ "
  echo ""
  echo -e "\e[0m"
}

onWarning() {
  echo -e "\e[33m\n"
  echo " █████   ███   █████   █████████   ███████████   ██████   █████ █████ ██████   █████   █████████   "
  echo " ░░███   ░███  ░░███   ███░░░░░███ ░░███░░░░░███ ░░██████ ░░███ ░░███ ░░██████ ░░███   ███░░░░░███ "
  echo "  ░███   ░███   ░███  ░███    ░███  ░███    ░███  ░███░███ ░███  ░███  ░███░███ ░███  ███     ░░░  "
  echo "  ░███   ░███   ░███  ░███████████  ░██████████   ░███░░███░███  ░███  ░███░░███░███ ░███          "
  echo "  ░░███  █████  ███   ░███░░░░░███  ░███░░░░░███  ░███ ░░██████  ░███  ░███ ░░██████ ░███    █████ "
  echo "   ░░░█████░█████░    ░███    ░███  ░███    ░███  ░███  ░░█████  ░███  ░███  ░░█████ ░░███  ░░███  "
  echo "     ░░███ ░░███      █████   █████ █████   █████ █████  ░░█████ █████ █████  ░░█████ ░░█████████  "
  echo "      ░░░   ░░░      ░░░░░   ░░░░░ ░░░░░   ░░░░░ ░░░░░    ░░░░░ ░░░░░ ░░░░░    ░░░░░   ░░░░░░░░░   "
  echo -e "\e[0m"
}

doRemount() {
  $ADB root
  sleep 3
  set +e
  REMOUNT_OUTPUT="$($ADB remount -R 2>&1)"
  REMOUNT_OUTPUT="${REMOUNT_OUTPUT,,}"
  REMOUNT_RESULT=$?
  echo "$REMOUNT_OUTPUT"
  if [[ $REMOUNT_RESULT != 0 || "$REMOUNT_OUTPUT" == *"remount failed"* ]]; then
    exit 1
  fi
  set -e
  if [[ "$REMOUNT_OUTPUT" == *"rebooting"* ]]
  then
    echo -e "\e[93m"
    echo "██████╗ ███████╗██████╗  ██████╗  ██████╗ ████████╗██╗███╗   ██╗ ██████╗ "
    echo "██╔══██╗██╔════╝██╔══██╗██╔═══██╗██╔═══██╗╚══██╔══╝██║████╗  ██║██╔════╝ "
    echo "██████╔╝█████╗  ██████╔╝██║   ██║██║   ██║   ██║   ██║██╔██╗ ██║██║  ███╗"
    echo "██╔══██╗██╔══╝  ██╔══██╗██║   ██║██║   ██║   ██║   ██║██║╚██╗██║██║   ██║"
    echo "██║  ██║███████╗██████╔╝╚██████╔╝╚██████╔╝   ██║   ██║██║ ╚████║╚██████╔╝"
    echo "╚═╝  ╚═╝╚══════╝╚═════╝  ╚═════╝  ╚═════╝    ╚═╝   ╚═╝╚═╝  ╚═══╝ ╚═════╝ "
    echo "                                                                         "
    echo -e "\e[0m"
    sleep 5
    $ADB wait-for-device root && $ADB wait-for-device remount
  fi
}

connectedProduct() {
  echo "$($ADB shell getprop ro.build.product)"
}

checkConnectedProduct() {
  checkDeviceRev

  # Make sure connected device matches $TARGET_PRODUCT
  CONNECTED_PRODUCT="$(connectedProduct)"
  if [ "$TARGET_PRODUCT" != "$CONNECTED_PRODUCT" ]
  then
    echo "ERROR: Connected device ($CONNECTED_PRODUCT) does not match TARGET_PRODUCT. Please lunch $CONNECTED_PRODUCT first"
    exit 1
  fi
}

checkDeviceRev() {
  set +e
  GETPROP_OUTPUT="$($ADB shell getprop ro.boot.hardware.revision 2>&1)"
  GETPROP_RESULT=$?
  if [ $GETPROP_RESULT -ne 0 ]
  then
    echo "Cannot check device status via adb, please check device connection"
    exit 1
  elif [[ "$GETPROP_OUTPUT" =~ ^([DP]VT|MP).* ]]
  then
    echo "Device must be EVT or earlier for CHRE development"
    exit 1
  fi
  set -e
}
