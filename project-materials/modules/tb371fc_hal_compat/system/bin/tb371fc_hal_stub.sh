#!/system/bin/sh

# Keep the service process alive so init does not restart the broken vendor HAL.
while true; do
  sleep 3600
done
