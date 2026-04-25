#!/bin/bash
set -euo pipefail

ANDROID_GIT_REVISION=${1:-dfd2f7baf96825834f784900ce644e9ead8a9a89}
DESKTOP_GIT_REVISION=${1:-60a1e125452ee672d8747564d0055d5bfec9f679}

update_proto() {
  case "$1" in
    Signal-Android)
      REPO="Signal-Android"
      prefix="lib/libsignal-service/src/main/protowire/"
      GIT_REVISION=$ANDROID_GIT_REVISION
      ;;
    Signal-Android-Archive)
      REPO="Signal-Android"
      prefix="lib/archive/src/main/protowire/"
      GIT_REVISION=$ANDROID_GIT_REVISION
      ;;
    Signal-Desktop)
      REPO="Signal-Desktop"
      prefix="protos/"
      GIT_REVISION=$DESKTOP_GIT_REVISION
      ;;
  esac
  echo https://raw.githubusercontent.com/signalapp/${REPO}/${GIT_REVISION}/${prefix}${2}
  curl -LOf https://raw.githubusercontent.com/signalapp/${REPO}/${GIT_REVISION}/${prefix}${2}
}


update_proto Signal-Android Groups.proto
update_proto Signal-Android Provisioning.proto
update_proto Signal-Android SignalService.proto
update_proto Signal-Android StickerResources.proto
update_proto Signal-Android WebSocketResources.proto
update_proto Signal-Android StorageService.proto

update_proto Signal-Android-Archive Backup.proto
mv Backup.proto backuppb/Backup.proto

update_proto Signal-Desktop DeviceName.proto
# TODO these were moved to libsignal only
#update_proto Signal-Desktop UnidentifiedDelivery.proto
#update_proto Signal-Desktop ContactDiscovery.proto
