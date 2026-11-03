# Kernel starts after its read-only preflight and a 30-second delay.
# Allow the bounded 120-second modem run and cleanup to finish.
step modem_offline
sleep 240
shot modem_offline
step modem_offline_done
