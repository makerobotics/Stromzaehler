# Stromzaehler
Read out electricity meter with nodemcu and send per MQTT into home network.

Hardware
-NodeMCU (ESP8266)
-10 KOhm pull-up resistor.
-TEKT5400S Phototransistor λp max920nm 70V 37° λd850-980nm (reading IR pulses)

Software
Connect to wifi and MQTT broker.
Read SML protocol IR frames by DIO. 
Decode Smartmeter sml frame and extract power and total consumption.
Publish to MQTT topic.


References:
https://wiki.volkszaehler.org/
https://forum.arduino.cc/index.php?topic=548325.0
https://www.photovoltaikforum.com/thread/112216-sml-protokoll-hilfe-gesucht-sml-esp8266-mqtt/