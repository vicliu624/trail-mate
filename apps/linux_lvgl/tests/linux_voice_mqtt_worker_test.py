"""Real voice/mesh worker pair through an independent loopback MQTT wire relay."""
import sys
from linux_mqtt_application_test import run

run(sys.argv[1], 'mt')
print('MQTT VMP worker: recorded public/private clips written, authenticated, recovered and persisted without RF')
